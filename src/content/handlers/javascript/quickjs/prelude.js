(function (native) {
    "use strict";

    var global = globalThis;
    var indexPattern = /^(0|[1-9][0-9]*)$/;

    function define(target, name, value) {
        Object.defineProperty(target, name, {
            value: value,
            writable: true,
            configurable: true,
            enumerable: false
        });
    }

    function listIndex(key) {
        if (typeof key === "string" && indexPattern.test(key)) {
            return Number(key);
        }
        return -1;
    }

    function domException(message, name) {
        return new DOMException(message, name);
    }

    function format(args) {
        if (args.length === 0) {
            return [""];
        }
        var target = args[0];
        if (args.length === 1 || typeof target !== "string") {
            return args.map(function (a) { return String(a); });
        }
        var result = "";
        var next = 1;
        var i = 0;
        while (i < target.length) {
            var c = target[i];
            if (c === "%" && i + 1 < target.length) {
                var spec = target[i + 1];
                if (spec === "%") {
                    result += "%";
                    i += 2;
                    continue;
                }
                if ("sdifoOc".indexOf(spec) >= 0 && next < args.length) {
                    var value = args[next++];
                    if (spec === "d" || spec === "i") {
                        result += String(parseInt(value, 10));
                    } else if (spec === "f") {
                        result += String(parseFloat(value));
                    } else if (spec === "o" || spec === "O") {
                        try {
                            result += JSON.stringify(value);
                        } catch (e) {
                            result += String(value);
                        }
                    } else if (spec !== "c") {
                        result += String(value);
                    }
                    i += 2;
                    continue;
                }
            }
            result += c;
            i += 1;
        }
        var out = [result];
        for (; next < args.length; next++) {
            out.push(String(args[next]));
        }
        return out;
    }

    function tokenListToString() {
        var parts = [];
        for (var index = 0; index < this.length; index++) {
            parts.push(this.item(index));
        }
        return parts.join(" ");
    }

    DOMTokenList.prototype.toString = tokenListToString;
    DOMSettableTokenList.prototype.toString = tokenListToString;

    Location.prototype.toString = function () {
        return this.href;
    };

    [NodeList, HTMLCollection, NamedNodeMap].forEach(function (list) {
        list.prototype[Symbol.iterator] = Array.prototype[Symbol.iterator];
    });
    NodeList.prototype.forEach = Array.prototype.forEach;

    /* Selectors */

    var selectorCache = new Map();

    function isNameChar(c) {
        return (c >= "a" && c <= "z") || (c >= "A" && c <= "Z") ||
            (c >= "0" && c <= "9") || c === "-" || c === "_" ||
            c.charCodeAt(0) >= 0x80;
    }

    function SelectorParser(text) {
        this.s = text;
        this.i = 0;
    }

    SelectorParser.prototype.fail = function () {
        throw domException("'" + this.s + "' is not a valid selector", "SyntaxError");
    };

    SelectorParser.prototype.peek = function () {
        return this.s[this.i];
    };

    SelectorParser.prototype.skipSpace = function () {
        var start = this.i;
        while (this.i < this.s.length && /\s/.test(this.s[this.i])) {
            this.i++;
        }
        return this.i > start;
    };

    SelectorParser.prototype.escape = function () {
        this.i++;
        var hex = "";
        while (hex.length < 6 && this.i < this.s.length && /[0-9a-fA-F]/.test(this.s[this.i])) {
            hex += this.s[this.i++];
        }
        if (hex.length > 0) {
            if (this.i < this.s.length && /\s/.test(this.s[this.i])) {
                this.i++;
            }
            var code = parseInt(hex, 16);
            if (code === 0 || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) {
                code = 0xfffd;
            }
            return String.fromCodePoint(code);
        }
        if (this.i >= this.s.length) {
            return "�";
        }
        return this.s[this.i++];
    };

    SelectorParser.prototype.ident = function () {
        var out = "";
        while (this.i < this.s.length) {
            var c = this.s[this.i];
            if (c === "\\") {
                out += this.escape();
            } else if (isNameChar(c)) {
                out += c;
                this.i++;
            } else {
                break;
            }
        }
        if (out === "" || out === "-" || /^-?[0-9]/.test(out)) {
            this.fail();
        }
        return out;
    };

    SelectorParser.prototype.string = function () {
        var quote = this.s[this.i++];
        var out = "";
        while (this.i < this.s.length && this.s[this.i] !== quote) {
            if (this.s[this.i] === "\\") {
                if (this.s[this.i + 1] === "\n") {
                    this.i += 2;
                } else {
                    out += this.escape();
                }
            } else {
                out += this.s[this.i++];
            }
        }
        if (this.s[this.i] !== quote) {
            this.fail();
        }
        this.i++;
        return out;
    };

    SelectorParser.prototype.raw = function () {
        var depth = 1;
        var start = this.i;
        while (this.i < this.s.length) {
            var c = this.s[this.i];
            if (c === "(") {
                depth++;
            } else if (c === ")") {
                depth--;
                if (depth === 0) {
                    var out = this.s.slice(start, this.i);
                    this.i++;
                    return out;
                }
            } else if (c === "\"" || c === "'") {
                this.string();
                continue;
            }
            this.i++;
        }
        this.fail();
    };

    SelectorParser.prototype.attribute = function () {
        this.i++;
        this.skipSpace();
        var name = this.ident();
        if (this.peek() === "|" && this.s[this.i + 1] !== "=") {
            this.i++;
            name = this.ident();
        }
        this.skipSpace();
        var attr = { name: name.toLowerCase(), op: null, value: null, ci: false };
        var c = this.peek();
        if (c === "]") {
            this.i++;
            return attr;
        }
        if (c === "=") {
            attr.op = "=";
            this.i++;
        } else if ("~|^$*".indexOf(c) >= 0 && this.s[this.i + 1] === "=") {
            attr.op = c + "=";
            this.i += 2;
        } else {
            this.fail();
        }
        this.skipSpace();
        c = this.peek();
        attr.value = (c === "\"" || c === "'") ? this.string() : this.ident();
        this.skipSpace();
        c = this.peek();
        if (c === "i" || c === "I" || c === "s" || c === "S") {
            attr.ci = c === "i" || c === "I";
            this.i++;
            this.skipSpace();
        }
        if (this.peek() !== "]") {
            this.fail();
        }
        this.i++;
        if (attr.ci) {
            attr.value = attr.value.toLowerCase();
        }
        return attr;
    };

    SelectorParser.prototype.pseudo = function () {
        this.i++;
        if (this.peek() === ":") {
            this.fail();
        }
        var name = this.ident().toLowerCase();
        var p = { name: name, arg: null };
        if (this.peek() === "(") {
            this.i++;
            if (name === "not" || name === "is" || name === "where" ||
                name === "matches" || name === "-webkit-any") {
                p.arg = this.list(false, true);
            } else if (name === "has") {
                p.arg = this.list(true, true);
            } else if (/^nth-/.test(name)) {
                p.arg = parseNth(this.raw(), this);
            } else {
                p.arg = this.raw().trim();
            }
        }
        if (!(name in pseudoClasses)) {
            this.fail();
        }
        if ((/^nth-|^not$|^is$|^where$|^has$|^lang$|^matches$|^-webkit-any$/.test(name)) !== (p.arg !== null)) {
            this.fail();
        }
        return p;
    };

    SelectorParser.prototype.compound = function () {
        var comp = { tag: null, ids: [], classes: [], attrs: [], pseudos: [], anchor: false };
        var any = false;
        var c = this.peek();
        if (c === "*") {
            this.i++;
            any = true;
        } else if (c !== undefined && (isNameChar(c) || c === "\\") && !/[0-9]/.test(c)) {
            comp.tag = this.ident().toLowerCase();
            any = true;
        }
        if (this.peek() === "|") {
            this.i++;
            if (this.peek() === "*") {
                this.i++;
                comp.tag = null;
            } else {
                comp.tag = this.ident().toLowerCase();
            }
        }
        for (;;) {
            c = this.peek();
            if (c === "#") {
                this.i++;
                comp.ids.push(this.ident());
            } else if (c === ".") {
                this.i++;
                comp.classes.push(this.ident());
            } else if (c === "[") {
                comp.attrs.push(this.attribute());
            } else if (c === ":") {
                comp.pseudos.push(this.pseudo());
            } else {
                break;
            }
            any = true;
        }
        if (!any) {
            this.fail();
        }
        return comp;
    };

    SelectorParser.prototype.complex = function (relative) {
        var compounds = [];
        var combinators = [];
        this.skipSpace();
        if (relative) {
            var lead = this.peek();
            compounds.push({ anchor: true });
            if (lead === ">" || lead === "+" || lead === "~") {
                this.i++;
                combinators.push(lead);
            } else {
                combinators.push(" ");
            }
            this.skipSpace();
        }
        compounds.push(this.compound());
        for (;;) {
            var spaced = this.skipSpace();
            var c = this.peek();
            if (c === undefined || c === "," || c === ")") {
                break;
            }
            if (c === ">" || c === "+" || c === "~") {
                this.i++;
                this.skipSpace();
                combinators.push(c);
            } else if (spaced) {
                combinators.push(" ");
            } else {
                this.fail();
            }
            compounds.push(this.compound());
        }
        return { compounds: compounds, combinators: combinators };
    };

    SelectorParser.prototype.list = function (relative, nested) {
        var list = [];
        for (;;) {
            list.push(this.complex(relative));
            this.skipSpace();
            var c = this.peek();
            if (c === ",") {
                this.i++;
                continue;
            }
            if (nested) {
                if (c !== ")") {
                    this.fail();
                }
                this.i++;
            } else if (c !== undefined) {
                this.fail();
            }
            return list;
        }
    };

    function parseNth(text, parser) {
        var t = text.trim().toLowerCase();
        if (t === "odd") {
            return { a: 2, b: 1 };
        }
        if (t === "even") {
            return { a: 2, b: 0 };
        }
        var m = /^([+-]?\d*)n\s*(?:([+-])\s*(\d+))?$/.exec(t);
        if (m) {
            var a = m[1] === "" || m[1] === "+" ? 1 : (m[1] === "-" ? -1 : parseInt(m[1], 10));
            var b = m[2] ? parseInt(m[3], 10) * (m[2] === "-" ? -1 : 1) : 0;
            return { a: a, b: b };
        }
        if (/^[+-]?\d+$/.test(t)) {
            return { a: 0, b: parseInt(t, 10) };
        }
        parser.fail();
    }

    function nthMatches(nth, pos) {
        if (nth.a === 0) {
            return pos === nth.b;
        }
        var diff = pos - nth.b;
        return diff % nth.a === 0 && diff / nth.a >= 0;
    }

    function parseSelectors(text) {
        var key = String(text);
        var cached = selectorCache.get(key);
        if (cached === undefined) {
            cached = new SelectorParser(key).list(false, false);
            if (selectorCache.size > 500) {
                selectorCache.clear();
            }
            selectorCache.set(key, cached);
        }
        return cached;
    }

    function elementIndex(el, ofType, fromEnd) {
        var pos = 1;
        var tag = ofType ? el.tagName : null;
        var sib = fromEnd ? el.nextElementSibling : el.previousElementSibling;
        while (sib !== null) {
            if (!ofType || sib.tagName === tag) {
                pos++;
            }
            sib = fromEnd ? sib.nextElementSibling : sib.previousElementSibling;
        }
        return pos;
    }

    function isCustomName(name) {
        return name.indexOf("-") > 0;
    }

    var formControls = { button: 1, input: 1, select: 1, textarea: 1, optgroup: 1, option: 1, fieldset: 1 };

    var pseudoClasses = {
        "root": function (el) {
            var p = el.parentNode;
            return p !== null && p.nodeType === 9;
        },
        "empty": function (el) {
            for (var n = el.firstChild; n !== null; n = n.nextSibling) {
                var t = n.nodeType;
                if (t === 1 || ((t === 3 || t === 4) && n.nodeValue.length > 0)) {
                    return false;
                }
            }
            return true;
        },
        "first-child": function (el) { return el.previousElementSibling === null; },
        "last-child": function (el) { return el.nextElementSibling === null; },
        "only-child": function (el) {
            return el.previousElementSibling === null && el.nextElementSibling === null;
        },
        "first-of-type": function (el) { return elementIndex(el, true, false) === 1; },
        "last-of-type": function (el) { return elementIndex(el, true, true) === 1; },
        "only-of-type": function (el) {
            return elementIndex(el, true, false) === 1 && elementIndex(el, true, true) === 1;
        },
        "nth-child": function (el, arg) { return nthMatches(arg, elementIndex(el, false, false)); },
        "nth-last-child": function (el, arg) { return nthMatches(arg, elementIndex(el, false, true)); },
        "nth-of-type": function (el, arg) { return nthMatches(arg, elementIndex(el, true, false)); },
        "nth-last-of-type": function (el, arg) { return nthMatches(arg, elementIndex(el, true, true)); },
        "not": function (el, arg, ctx) { return !matchesList(el, arg, ctx); },
        "is": function (el, arg, ctx) { return matchesList(el, arg, ctx); },
        "where": function (el, arg, ctx) { return matchesList(el, arg, ctx); },
        "matches": function (el, arg, ctx) { return matchesList(el, arg, ctx); },
        "-webkit-any": function (el, arg, ctx) { return matchesList(el, arg, ctx); },
        "has": function (el, arg, ctx) {
            var inner = { scope: ctx.scope, anchor: el };
            return arg.some(function (rel) {
                var comb = rel.combinators[0];
                var found = false;
                var test = function (cand) {
                    if (matchesComplex(cand, rel, inner)) {
                        found = true;
                        return true;
                    }
                    return false;
                };
                if (comb === " " || comb === ">") {
                    walk(el, test);
                } else {
                    for (var sib = el.nextElementSibling; sib !== null && !found; sib = sib.nextElementSibling) {
                        if (!test(sib)) {
                            walk(sib, test);
                        }
                        if (comb === "+") {
                            break;
                        }
                    }
                }
                return found;
            });
        },
        "scope": function (el, arg, ctx) {
            return ctx.scope ? el === ctx.scope : pseudoClasses.root(el);
        },
        "link": function (el) {
            var t = el.tagName.toLowerCase();
            return (t === "a" || t === "area" || t === "link") && el.hasAttribute("href");
        },
        "any-link": function (el) { return pseudoClasses.link(el); },
        "visited": function () { return false; },
        "hover": function () { return false; },
        "active": function () { return false; },
        "focus": function () { return false; },
        "focus-within": function () { return false; },
        "focus-visible": function () { return false; },
        "target": function (el) {
            var hash = location.hash;
            return hash.length > 1 && el.id === decodeURIComponent(hash.slice(1));
        },
        "checked": function (el) {
            var t = el.tagName.toLowerCase();
            if (t === "input") {
                var type = (el.getAttribute("type") || "").toLowerCase();
                return (type === "checkbox" || type === "radio") && el.hasAttribute("checked");
            }
            return t === "option" && el.hasAttribute("selected");
        },
        "disabled": function (el) {
            return el.tagName.toLowerCase() in formControls && el.hasAttribute("disabled");
        },
        "enabled": function (el) {
            return el.tagName.toLowerCase() in formControls && !el.hasAttribute("disabled");
        },
        "required": function (el) {
            var t = el.tagName.toLowerCase();
            return (t === "input" || t === "select" || t === "textarea") && el.hasAttribute("required");
        },
        "optional": function (el) {
            var t = el.tagName.toLowerCase();
            return (t === "input" || t === "select" || t === "textarea") && !el.hasAttribute("required");
        },
        "read-write": function (el) {
            var t = el.tagName.toLowerCase();
            if (t === "input" || t === "textarea") {
                return !el.hasAttribute("readonly") && !el.hasAttribute("disabled");
            }
            var ce = el.getAttribute("contenteditable");
            return ce !== null && ce.toLowerCase() !== "false";
        },
        "read-only": function (el) { return !pseudoClasses["read-write"](el); },
        "placeholder-shown": function (el) {
            var t = el.tagName.toLowerCase();
            return (t === "input" || t === "textarea") && el.hasAttribute("placeholder") &&
                (el.getAttribute("value") || "") === "";
        },
        "defined": function (el) {
            var name = el.tagName.toLowerCase();
            return !isCustomName(name) || customState.has(el);
        },
        "lang": function (el, arg) {
            var want = arg.replace(/^["']|["']$/g, "").toLowerCase();
            for (var n = el; n !== null && n.nodeType === 1; n = n.parentNode) {
                var lang = n.getAttribute("lang");
                if (lang !== null) {
                    lang = lang.toLowerCase();
                    return lang === want || lang.indexOf(want + "-") === 0;
                }
            }
            return false;
        }
    };

    function matchesAttr(el, attr) {
        var value = el.getAttribute(attr.name);
        if (value === null) {
            return false;
        }
        if (attr.op === null) {
            return true;
        }
        var want = attr.value;
        if (attr.ci) {
            value = value.toLowerCase();
        }
        switch (attr.op) {
        case "=":
            return value === want;
        case "~=":
            return want !== "" && !/\s/.test(want) && value.split(/\s+/).indexOf(want) >= 0;
        case "|=":
            return value === want || value.indexOf(want + "-") === 0;
        case "^=":
            return want !== "" && value.indexOf(want) === 0;
        case "$=":
            return want !== "" && value.length >= want.length &&
                value.lastIndexOf(want) === value.length - want.length;
        case "*=":
            return want !== "" && value.indexOf(want) >= 0;
        }
        return false;
    }

    function matchesCompound(el, comp, ctx) {
        if (comp.anchor) {
            return el === ctx.anchor;
        }
        if (comp.tag !== null && el.tagName.toLowerCase() !== comp.tag) {
            return false;
        }
        var i;
        for (i = 0; i < comp.ids.length; i++) {
            if (el.id !== comp.ids[i]) {
                return false;
            }
        }
        if (comp.classes.length > 0) {
            var cls = " " + el.className.split(/\s+/).join(" ") + " ";
            for (i = 0; i < comp.classes.length; i++) {
                if (cls.indexOf(" " + comp.classes[i] + " ") < 0) {
                    return false;
                }
            }
        }
        for (i = 0; i < comp.attrs.length; i++) {
            if (!matchesAttr(el, comp.attrs[i])) {
                return false;
            }
        }
        for (i = 0; i < comp.pseudos.length; i++) {
            var p = comp.pseudos[i];
            if (!pseudoClasses[p.name](el, p.arg, ctx)) {
                return false;
            }
        }
        return true;
    }

    function parentElementOf(el) {
        var p = el.parentNode;
        return p !== null && p.nodeType === 1 ? p : null;
    }

    function matchesFrom(el, sel, k, ctx) {
        if (k === 0) {
            return true;
        }
        var comb = sel.combinators[k - 1];
        var comp = sel.compounds[k - 1];
        var n;
        switch (comb) {
        case ">":
            n = parentElementOf(el);
            return n !== null && matchesCompound(n, comp, ctx) && matchesFrom(n, sel, k - 1, ctx);
        case " ":
            for (n = parentElementOf(el); n !== null; n = parentElementOf(n)) {
                if (matchesCompound(n, comp, ctx) && matchesFrom(n, sel, k - 1, ctx)) {
                    return true;
                }
            }
            return false;
        case "+":
            n = el.previousElementSibling;
            return n !== null && matchesCompound(n, comp, ctx) && matchesFrom(n, sel, k - 1, ctx);
        case "~":
            for (n = el.previousElementSibling; n !== null; n = n.previousElementSibling) {
                if (matchesCompound(n, comp, ctx) && matchesFrom(n, sel, k - 1, ctx)) {
                    return true;
                }
            }
            return false;
        }
        return false;
    }

    function matchesComplex(el, sel, ctx) {
        var last = sel.compounds.length - 1;
        return matchesCompound(el, sel.compounds[last], ctx) && matchesFrom(el, sel, last, ctx);
    }

    function matchesList(el, list, ctx) {
        for (var i = 0; i < list.length; i++) {
            if (matchesComplex(el, list[i], ctx)) {
                return true;
            }
        }
        return false;
    }

    function firstElementOf(root) {
        switch (root.nodeType) {
        case 1:
            return root.firstElementChild;
        case 9:
            return root.documentElement;
        default:
            for (var n = root.firstChild; n !== null; n = n.nextSibling) {
                if (n.nodeType === 1) {
                    return n;
                }
            }
            return null;
        }
    }

    function walk(root, fn) {
        var n = firstElementOf(root);
        while (n !== null) {
            if (fn(n)) {
                return true;
            }
            var next = n.firstElementChild;
            while (next === null) {
                next = n.nextElementSibling;
                if (next === null) {
                    n = n.parentNode;
                    if (n === null || n === root) {
                        return false;
                    }
                }
            }
            n = next;
        }
        return false;
    }

    function scopeOf(root) {
        return root.nodeType === 1 ? root : null;
    }

    function StaticNodeList(items) {
        for (var i = 0; i < items.length; i++) {
            Object.defineProperty(this, i, { value: items[i], enumerable: true });
        }
        Object.defineProperty(this, "length", { value: items.length });
        Object.freeze(this);
    }
    StaticNodeList.prototype = Object.create(NodeList.prototype);
    define(StaticNodeList.prototype, "constructor", NodeList);
    Object.defineProperty(StaticNodeList.prototype, "length", { value: 0, writable: true, configurable: true });
    define(StaticNodeList.prototype, "item", function (index) {
        var v = this[index >>> 0];
        return v === undefined ? null : v;
    });
    ["forEach", "entries", "keys", "values"].forEach(function (name) {
        define(StaticNodeList.prototype, name, Array.prototype[name]);
    });
    define(StaticNodeList.prototype, Symbol.iterator, Array.prototype.values);

    function querySelector(selectors) {
        var list = parseSelectors(selectors);
        var ctx = { scope: scopeOf(this), anchor: null };
        if (this.nodeType === 9 && list.length === 1 && list[0].compounds.length === 1) {
            var c = list[0].compounds[0];
            if (c.ids.length === 1 && c.tag === null && c.classes.length === 0 &&
                c.attrs.length === 0 && c.pseudos.length === 0) {
                return this.getElementById(c.ids[0]);
            }
        }
        var found = null;
        walk(this, function (el) {
            if (matchesList(el, list, ctx)) {
                found = el;
                return true;
            }
            return false;
        });
        return found;
    }

    function querySelectorAll(selectors) {
        var list = parseSelectors(selectors);
        var ctx = { scope: scopeOf(this), anchor: null };
        var out = [];
        walk(this, function (el) {
            if (matchesList(el, list, ctx)) {
                out.push(el);
            }
            return false;
        });
        return new StaticNodeList(out);
    }

    [Document, Element, global.DocumentFragment].forEach(function (iface) {
        if (typeof iface === "function") {
            define(iface.prototype, "querySelector", querySelector);
            define(iface.prototype, "querySelectorAll", querySelectorAll);
        }
    });

    define(Element.prototype, "matches", function (selectors) {
        return matchesList(this, parseSelectors(selectors), { scope: this, anchor: null });
    });
    define(Element.prototype, "webkitMatchesSelector", Element.prototype.matches);
    define(Element.prototype, "closest", function (selectors) {
        var list = parseSelectors(selectors);
        var ctx = { scope: this, anchor: null };
        for (var el = this; el !== null; el = parentElementOf(el)) {
            if (matchesList(el, list, ctx)) {
                return el;
            }
        }
        return null;
    });

    /* Media queries */

    var mediaFeatures = {
        "width": function () { return { type: "length", value: global.innerWidth }; },
        "height": function () { return { type: "length", value: global.innerHeight }; },
        "device-width": function () { return { type: "length", value: global.outerWidth }; },
        "device-height": function () { return { type: "length", value: global.outerHeight }; },
        "aspect-ratio": function () { return { type: "ratio", value: global.innerWidth / Math.max(1, global.innerHeight) }; },
        "device-aspect-ratio": function () { return { type: "ratio", value: global.outerWidth / Math.max(1, global.outerHeight) }; },
        "resolution": function () { return { type: "resolution", value: global.devicePixelRatio }; },
        "-webkit-device-pixel-ratio": function () { return { type: "number", value: global.devicePixelRatio }; },
        "orientation": function () { return { type: "ident", value: global.innerHeight >= global.innerWidth ? "portrait" : "landscape" }; },
        "color": function () { return { type: "number", value: 8 }; },
        "color-index": function () { return { type: "number", value: 0 }; },
        "monochrome": function () { return { type: "number", value: 0 }; },
        "grid": function () { return { type: "number", value: 0 }; },
        "prefers-color-scheme": function () { return { type: "ident", value: "light" }; },
        "prefers-reduced-motion": function () { return { type: "ident", value: "no-preference" }; },
        "prefers-reduced-transparency": function () { return { type: "ident", value: "no-preference" }; },
        "prefers-contrast": function () { return { type: "ident", value: "no-preference" }; },
        "forced-colors": function () { return { type: "ident", value: "none" }; },
        "inverted-colors": function () { return { type: "ident", value: "none" }; },
        "hover": function () { return { type: "ident", value: "hover" }; },
        "any-hover": function () { return { type: "ident", value: "hover" }; },
        "pointer": function () { return { type: "ident", value: "fine" }; },
        "any-pointer": function () { return { type: "ident", value: "fine" }; },
        "scripting": function () { return { type: "ident", value: "enabled" }; },
        "update": function () { return { type: "ident", value: "fast" }; },
        "display-mode": function () { return { type: "ident", value: "browser" }; },
        "color-gamut": function () { return { type: "ident", value: "srgb" }; },
        "dynamic-range": function () { return { type: "ident", value: "standard" }; },
        "overflow-block": function () { return { type: "ident", value: "scroll" }; },
        "overflow-inline": function () { return { type: "ident", value: "scroll" }; }
    };

    var falseIdents = { "none": 1, "no-preference": 1 };

    function mediaValue(text, type) {
        var t = text.trim();
        var m;
        if (type === "ratio") {
            m = /^([0-9.]+)\s*(?:\/\s*([0-9.]+))?$/.exec(t);
            return m ? parseFloat(m[1]) / (m[2] ? parseFloat(m[2]) : 1) : NaN;
        }
        if (type === "ident") {
            return t;
        }
        m = /^([+-]?[0-9]*\.?[0-9]+(?:e[+-]?[0-9]+)?)([a-z%]*)$/.exec(t);
        if (!m) {
            return NaN;
        }
        var n = parseFloat(m[1]);
        var unit = m[2];
        if (type === "length") {
            switch (unit) {
            case "px": case "": return unit === "" && n !== 0 ? NaN : n;
            case "em": case "rem": return n * 16;
            case "ex": return n * 8;
            case "ch": return n * 8;
            case "vw": return n * global.innerWidth / 100;
            case "vh": return n * global.innerHeight / 100;
            case "vmin": return n * Math.min(global.innerWidth, global.innerHeight) / 100;
            case "vmax": return n * Math.max(global.innerWidth, global.innerHeight) / 100;
            case "cm": return n * 96 / 2.54;
            case "mm": return n * 96 / 25.4;
            case "q": return n * 96 / 101.6;
            case "in": return n * 96;
            case "pt": return n * 96 / 72;
            case "pc": return n * 16;
            }
            return NaN;
        }
        if (type === "resolution") {
            switch (unit) {
            case "dppx": case "x": return n;
            case "dpi": return n / 96;
            case "dpcm": return n * 2.54 / 96;
            }
            return NaN;
        }
        return unit === "" ? n : NaN;
    }

    function compareMedia(actual, op, want) {
        var eps = 1e-6;
        switch (op) {
        case "=": case ":": return Math.abs(actual - want) < eps;
        case "<": return actual < want - eps;
        case "<=": return actual <= want + eps;
        case ">": return actual > want + eps;
        case ">=": return actual >= want - eps;
        }
        return false;
    }

    function flipOp(op) {
        return { "<": ">", "<=": ">=", ">": "<", ">=": "<=", "=": "=" }[op];
    }

    function evaluateFeature(text) {
        var t = text.trim();
        var m = /^([a-z-]+)\s*:\s*(.+)$/.exec(t);
        var name, feature, value;
        if (m) {
            name = m[1];
            var prefix = null;
            if (/^(min|max)-/.test(name) && !(name in mediaFeatures)) {
                prefix = name.slice(0, 3);
                name = name.slice(4);
            }
            if (name.indexOf("-webkit-min-") === 0 || name.indexOf("-webkit-max-") === 0) {
                prefix = name.slice(8, 11);
                name = "-webkit-" + name.slice(12);
            }
            if (!(name in mediaFeatures)) {
                return false;
            }
            feature = mediaFeatures[name]();
            if (feature.type === "ident") {
                return prefix === null && feature.value === m[2].trim();
            }
            value = mediaValue(m[2], feature.type);
            if (isNaN(value)) {
                return false;
            }
            return compareMedia(feature.value, prefix === "min" ? ">=" : (prefix === "max" ? "<=" : "="), value);
        }
        if (/^[a-z-]+$/.test(t)) {
            if (!(t in mediaFeatures)) {
                return false;
            }
            feature = mediaFeatures[t]();
            return feature.type === "ident" ? !(feature.value in falseIdents) : feature.value !== 0;
        }
        var parts = t.split(/\s*(<=|>=|<|>|=)\s*/);
        if (parts.length === 3 || parts.length === 5) {
            var nameIndex = /^[a-z-]+$/.test(parts[0]) && parts[0] in mediaFeatures ? 0 : 2;
            name = parts[nameIndex];
            if (!(name in mediaFeatures)) {
                return false;
            }
            feature = mediaFeatures[name]();
            if (feature.type === "ident") {
                return false;
            }
            if (nameIndex === 0) {
                value = mediaValue(parts[2], feature.type);
                return !isNaN(value) && compareMedia(feature.value, parts[1], value) && parts.length === 3;
            }
            value = mediaValue(parts[0], feature.type);
            if (isNaN(value) || !compareMedia(feature.value, flipOp(parts[1]), value)) {
                return false;
            }
            if (parts.length === 5) {
                value = mediaValue(parts[4], feature.type);
                return !isNaN(value) && compareMedia(feature.value, parts[3], value);
            }
            return true;
        }
        return false;
    }

    function splitTopLevel(text, sep) {
        var out = [];
        var depth = 0;
        var start = 0;
        for (var i = 0; i < text.length; i++) {
            var c = text[i];
            if (c === "(") {
                depth++;
            } else if (c === ")") {
                depth--;
            } else if (depth === 0 && c === sep) {
                out.push(text.slice(start, i));
                start = i + 1;
            }
        }
        out.push(text.slice(start));
        return out;
    }

    function evaluateCondition(text) {
        var t = text.trim();
        var tokens = [];
        var i = 0;
        while (i < t.length) {
            if (/\s/.test(t[i])) {
                i++;
                continue;
            }
            if (t[i] === "(") {
                var depth = 1;
                var start = ++i;
                while (i < t.length && depth > 0) {
                    if (t[i] === "(") {
                        depth++;
                    } else if (t[i] === ")") {
                        depth--;
                    }
                    i++;
                }
                if (depth !== 0) {
                    return false;
                }
                tokens.push({ group: t.slice(start, i - 1) });
                continue;
            }
            var m = /^[a-z-]+/.exec(t.slice(i));
            if (!m) {
                return false;
            }
            tokens.push({ word: m[0] });
            i += m[0].length;
        }
        var result = null;
        var joiner = null;
        for (var k = 0; k < tokens.length; k++) {
            var negate = false;
            if (tokens[k].word === "not") {
                negate = true;
                k++;
            }
            var tok = tokens[k];
            if (!tok || tok.group === undefined) {
                return false;
            }
            var inner = tok.group.trim();
            var v = inner[0] === "(" || /^not\s*\(/.test(inner) ? evaluateCondition(inner) : evaluateFeature(inner);
            if (negate) {
                v = !v;
            }
            if (result === null) {
                result = v;
            } else if (joiner === "and") {
                result = result && v;
            } else if (joiner === "or") {
                result = result || v;
            } else {
                return false;
            }
            if (k + 1 < tokens.length) {
                var w = tokens[++k].word;
                if (w !== "and" && w !== "or") {
                    return false;
                }
                if (joiner !== null && joiner !== w) {
                    return false;
                }
                joiner = w;
                if (k + 1 >= tokens.length) {
                    return false;
                }
            }
        }
        return result === true;
    }

    function evaluateQuery(text) {
        var t = text.trim();
        if (t === "") {
            return true;
        }
        var m = /^(?:(only|not)\s+)?([a-z-]+)(?:\s+and\s+([\s\S]+))?$/.exec(t);
        if (m && m[2] !== "not" && m[2] !== "only" && t[0] !== "(") {
            var typeMatches = m[2] === "all" || m[2] === "screen";
            var result = typeMatches && (m[3] === undefined || evaluateCondition(m[3]));
            return m[1] === "not" ? !result : result;
        }
        return evaluateCondition(t);
    }

    function evaluateMedia(query) {
        return splitTopLevel(String(query).toLowerCase(), ",").some(evaluateQuery);
    }

    var watchedQueries = new Set();
    var mediaTimer = null;

    function pollMedia() {
        watchedQueries.forEach(function (mql) {
            var now = evaluateMedia(mql.media);
            if (now !== mql._last) {
                mql._last = now;
                mql._dispatch(now);
            }
        });
    }

    function updateMediaWatch(mql) {
        if (mql._listeners.length > 0 || typeof mql.onchange === "function") {
            watchedQueries.add(mql);
        } else {
            watchedQueries.delete(mql);
        }
        if (watchedQueries.size > 0 && mediaTimer === null) {
            mediaTimer = global.setInterval(pollMedia, 250);
        } else if (watchedQueries.size === 0 && mediaTimer !== null) {
            global.clearInterval(mediaTimer);
            mediaTimer = null;
        }
    }

    function MediaQueryList(media) {
        var onchange = null;
        define(this, "_listeners", []);
        define(this, "_last", evaluateMedia(media));
        Object.defineProperty(this, "media", { value: String(media), enumerable: true });
        Object.defineProperty(this, "onchange", {
            get: function () { return onchange; },
            set: function (fn) {
                onchange = typeof fn === "function" ? fn : null;
                updateMediaWatch(this);
            },
            enumerable: true
        });
    }
    Object.defineProperty(MediaQueryList.prototype, "matches", {
        get: function () { return evaluateMedia(this.media); },
        enumerable: true,
        configurable: true
    });
    define(MediaQueryList.prototype, "addListener", function (fn) {
        this.addEventListener("change", fn);
    });
    define(MediaQueryList.prototype, "removeListener", function (fn) {
        this.removeEventListener("change", fn);
    });
    define(MediaQueryList.prototype, "addEventListener", function (type, fn) {
        if (type === "change" && fn !== null && fn !== undefined && this._listeners.indexOf(fn) < 0) {
            this._listeners.push(fn);
            updateMediaWatch(this);
        }
    });
    define(MediaQueryList.prototype, "removeEventListener", function (type, fn) {
        var idx = this._listeners.indexOf(fn);
        if (type === "change" && idx >= 0) {
            this._listeners.splice(idx, 1);
            updateMediaWatch(this);
        }
    });
    define(MediaQueryList.prototype, "dispatchEvent", function (event) {
        var self = this;
        var handlers = this._listeners.slice();
        if (typeof this.onchange === "function") {
            handlers.unshift(this.onchange);
        }
        handlers.forEach(function (fn) {
            try {
                if (typeof fn === "function") {
                    fn.call(self, event);
                } else if (fn && typeof fn.handleEvent === "function") {
                    fn.handleEvent(event);
                }
            } catch (e) {
                console.error(e);
            }
        });
        return true;
    });
    define(MediaQueryList.prototype, "_dispatch", function (matches) {
        this.dispatchEvent({ type: "change", matches: matches, media: this.media, target: this, currentTarget: this });
    });
    define(MediaQueryList.prototype, Symbol.toStringTag, "MediaQueryList");

    define(global, "MediaQueryList", MediaQueryList);
    define(Window.prototype, "matchMedia", function (query) {
        if (arguments.length < 1) {
            throw new TypeError("matchMedia requires 1 argument");
        }
        return new MediaQueryList(query);
    });

    /* Custom elements */

    var customState = new WeakMap();
    var pendingDisconnect = new WeakSet();
    var definitions = new Map();
    var definitionsByCtor = new Map();
    var pendingDefinitions = new Map();
    var constructionStack = [];
    var reservedNames = {
        "annotation-xml": 1, "color-profile": 1, "font-face": 1, "font-face-src": 1,
        "font-face-uri": 1, "font-face-format": 1, "font-face-name": 1, "missing-glyph": 1
    };
    var nativeHTMLElement = HTMLElement;
    var nativeCreateElement = Document.prototype.createElement;
    var listening = false;

    function validCustomName(name) {
        return /^[a-z][a-z0-9._·À-￿-]*-[a-z0-9._·À-￿-]*$/.test(name) &&
            !(name in reservedNames);
    }

    function reportError(e) {
        try {
            console.error(e instanceof Error ? e.stack ? e + "\n" + e.stack : String(e) : String(e));
        } catch (ignored) {
        }
    }

    function isConnected(node) {
        for (var n = node; n !== null; n = n.parentNode) {
            if (n.nodeType === 9) {
                return true;
            }
        }
        return false;
    }

    function callback(el, name, args) {
        var def = customState.get(el);
        if (def && typeof def[name] === "function") {
            try {
                def[name].apply(el, args || []);
            } catch (e) {
                reportError(e);
            }
        }
    }

    function upgrade(el, def) {
        if (customState.has(el)) {
            return;
        }
        constructionStack.push(el);
        try {
            var result = new def.ctor();
            if (result !== el) {
                throw new TypeError("custom element constructor did not produce the element");
            }
        } catch (e) {
            constructionStack.pop();
            reportError(e);
            return;
        }
        constructionStack.pop();
        customState.set(el, def);
        def.observed.forEach(function (attr) {
            var value = el.getAttribute(attr);
            if (value !== null) {
                callback(el, "attributeChangedCallback", [attr, null, value, null]);
            }
        });
        if (isConnected(el)) {
            callback(el, "connectedCallback");
        }
    }

    function upgradeTree(root, connected) {
        var visit = function (el) {
            var name = el.tagName.toLowerCase();
            if (isCustomName(name)) {
                if (!customState.has(el)) {
                    var def = definitions.get(name);
                    if (def) {
                        upgrade(el, def);
                    }
                } else if (connected) {
                    if (pendingDisconnect.has(el)) {
                        pendingDisconnect.delete(el);
                        callback(el, "disconnectedCallback");
                    }
                    callback(el, "connectedCallback");
                }
            }
            return false;
        };
        if (root.nodeType === 1) {
            visit(root);
        }
        walk(root, visit);
    }

    function disconnectTree(root) {
        var found = [];
        var visit = function (el) {
            if (customState.has(el) && !pendingDisconnect.has(el)) {
                pendingDisconnect.add(el);
                found.push(el);
            }
            return false;
        };
        if (root.nodeType === 1) {
            visit(root);
        }
        walk(root, visit);
        if (found.length > 0) {
            queueMicrotask(function () {
                found.forEach(function (el) {
                    if (pendingDisconnect.has(el)) {
                        pendingDisconnect.delete(el);
                        callback(el, "disconnectedCallback");
                    }
                });
            });
        }
    }

    function startListening() {
        if (listening) {
            return;
        }
        listening = true;
        document.addEventListener("DOMNodeInserted", function (e) {
            var target = e.target;
            if (target !== null && (target.nodeType === 1 || target.nodeType === 11) && isConnected(target)) {
                upgradeTree(target, true);
            }
        });
        document.addEventListener("DOMNodeRemoved", function (e) {
            var target = e.target;
            if (target !== null && target.nodeType === 1) {
                disconnectTree(target);
            }
        });
        document.addEventListener("DOMAttrModified", function (e) {
            var el = e.target;
            var def = el !== null ? customState.get(el) : undefined;
            if (def === undefined) {
                return;
            }
            var name = e.attrName;
            if (!def.observed.has(name)) {
                return;
            }
            var change = e.attrChange;
            callback(el, "attributeChangedCallback", [
                name,
                change === 2 ? null : e.prevValue,
                change === 3 ? null : e.newValue,
                null
            ]);
        });
    }

    function HTMLElementConstructor() {
        if (new.target === undefined) {
            throw new TypeError("Failed to construct 'HTMLElement': Please use the 'new' operator");
        }
        var def = definitionsByCtor.get(new.target);
        if (def === undefined) {
            throw new TypeError("Illegal constructor");
        }
        var el;
        if (constructionStack.length > 0 && constructionStack[constructionStack.length - 1] !== null) {
            el = constructionStack[constructionStack.length - 1];
            constructionStack[constructionStack.length - 1] = null;
        } else if (constructionStack.length > 0) {
            throw new TypeError("custom element constructor called twice");
        } else {
            el = nativeCreateElement.call(document, def.name);
            customState.set(el, def);
        }
        Object.setPrototypeOf(el, new.target.prototype);
        return el;
    }
    HTMLElementConstructor.prototype = nativeHTMLElement.prototype;
    define(nativeHTMLElement.prototype, "constructor", HTMLElementConstructor);
    Object.setPrototypeOf(HTMLElementConstructor, Object.getPrototypeOf(nativeHTMLElement));
    Object.defineProperty(HTMLElementConstructor, "name", { value: "HTMLElement" });
    define(global, "HTMLElement", HTMLElementConstructor);

    define(Document.prototype, "createElement", function (localName) {
        var el = nativeCreateElement.apply(this, arguments);
        if (el !== null && typeof el === "object" && this === document) {
            var def = definitions.get(String(localName).toLowerCase());
            if (def !== undefined && !customState.has(el)) {
                upgrade(el, def);
            }
        }
        return el;
    });

    function CustomElementRegistry() {
        throw new TypeError("Illegal constructor");
    }
    define(CustomElementRegistry.prototype, "define", function (name, ctor, options) {
        name = String(name);
        if (typeof ctor !== "function" || ctor.prototype === undefined) {
            throw new TypeError("Failed to execute 'define' on 'CustomElementRegistry': parameter 2 is not a constructor");
        }
        if (!validCustomName(name)) {
            throw domException("'" + name + "' is not a valid custom element name", "SyntaxError");
        }
        if (definitions.has(name)) {
            throw domException("the name '" + name + "' has already been used", "NotSupportedError");
        }
        if (definitionsByCtor.has(ctor)) {
            throw domException("this constructor has already been used", "NotSupportedError");
        }
        if (options && options.extends !== undefined) {
            throw domException("customized built-in elements are not supported", "NotSupportedError");
        }
        var proto = ctor.prototype;
        var observed = ctor.observedAttributes;
        var def = {
            name: name,
            ctor: ctor,
            observed: new Set(observed === undefined || observed === null ? [] : Array.from(observed, String)),
            connectedCallback: proto.connectedCallback,
            disconnectedCallback: proto.disconnectedCallback,
            adoptedCallback: proto.adoptedCallback,
            attributeChangedCallback: proto.attributeChangedCallback
        };
        if (typeof def.attributeChangedCallback !== "function") {
            def.observed = new Set();
        }
        definitions.set(name, def);
        definitionsByCtor.set(ctor, def);
        startListening();
        var existing = document.getElementsByTagName(name);
        var elements = [];
        for (var i = 0; i < existing.length; i++) {
            elements.push(existing.item(i));
        }
        elements.forEach(function (el) {
            upgrade(el, def);
        });
        var pending = pendingDefinitions.get(name);
        if (pending !== undefined) {
            pendingDefinitions.delete(name);
            pending.resolve(ctor);
        }
    });
    define(CustomElementRegistry.prototype, "get", function (name) {
        var def = definitions.get(String(name));
        return def === undefined ? undefined : def.ctor;
    });
    define(CustomElementRegistry.prototype, "getName", function (ctor) {
        var def = definitionsByCtor.get(ctor);
        return def === undefined ? null : def.name;
    });
    define(CustomElementRegistry.prototype, "whenDefined", function (name) {
        name = String(name);
        if (!validCustomName(name)) {
            return Promise.reject(domException("'" + name + "' is not a valid custom element name", "SyntaxError"));
        }
        var def = definitions.get(name);
        if (def !== undefined) {
            return Promise.resolve(def.ctor);
        }
        var pending = pendingDefinitions.get(name);
        if (pending === undefined) {
            pending = {};
            pending.promise = new Promise(function (resolve) {
                pending.resolve = resolve;
            });
            pendingDefinitions.set(name, pending);
        }
        return pending.promise;
    });
    define(CustomElementRegistry.prototype, "upgrade", function (root) {
        upgradeTree(root, false);
    });
    define(CustomElementRegistry.prototype, Symbol.toStringTag, "CustomElementRegistry");

    var registry = Object.create(CustomElementRegistry.prototype);
    define(global, "CustomElementRegistry", CustomElementRegistry);
    Object.defineProperty(Window.prototype, "customElements", {
        get: function () { return registry; },
        enumerable: true,
        configurable: true
    });

    /* Encoding */

    function bufferSource(value) {
        if (value instanceof ArrayBuffer) {
            return value;
        }
        if (ArrayBuffer.isView(value)) {
            return value;
        }
        throw new TypeError("The provided value is not of type '(ArrayBuffer or ArrayBufferView)'");
    }

    if (typeof global.TextEncoder !== "function") {
        var TextEncoder = function TextEncoder() {
            if (!new.target) {
                throw new TypeError("Constructor TextEncoder requires 'new'");
            }
        };
        Object.defineProperty(TextEncoder.prototype, "encoding", { get: function () { return "utf-8"; } });
        define(TextEncoder.prototype, "encode", function (input) {
            return new Uint8Array(native.utf8Encode(input === undefined ? "" : String(input)));
        });
        define(TextEncoder.prototype, "encodeInto", function (input, dest) {
            var bytes = new Uint8Array(native.utf8Encode(String(input)));
            var n = Math.min(bytes.length, dest.length);
            while (n > 0 && n < bytes.length && (bytes[n] & 0xc0) === 0x80) {
                n--;
            }
            dest.set(bytes.subarray(0, n));
            return { read: native.utf8Decode(bytes.subarray(0, n)).length, written: n };
        });
        define(global, "TextEncoder", TextEncoder);
    }

    if (typeof global.TextDecoder !== "function") {
        var TextDecoder = function TextDecoder(label, options) {
            if (!new.target) {
                throw new TypeError("Constructor TextDecoder requires 'new'");
            }
            var l = label === undefined ? "utf-8" : String(label).trim().toLowerCase();
            if (l !== "utf-8" && l !== "utf8" && l !== "unicode-1-1-utf-8") {
                throw new RangeError("The encoding label provided ('" + label + "') is invalid.");
            }
            define(this, "_fatal", !!(options && options.fatal));
            define(this, "_ignoreBOM", !!(options && options.ignoreBOM));
        };
        Object.defineProperty(TextDecoder.prototype, "encoding", { get: function () { return "utf-8"; } });
        Object.defineProperty(TextDecoder.prototype, "fatal", { get: function () { return this._fatal; } });
        Object.defineProperty(TextDecoder.prototype, "ignoreBOM", { get: function () { return this._ignoreBOM; } });
        define(TextDecoder.prototype, "decode", function (input) {
            if (input === undefined) {
                return "";
            }
            var text = native.utf8Decode(bufferSource(input));
            if (!this._ignoreBOM && text.charCodeAt(0) === 0xfeff) {
                text = text.slice(1);
            }
            return text;
        });
        define(global, "TextDecoder", TextDecoder);
    }

    /* Abort */

    function AbortSignal() {
        throw new TypeError("Illegal constructor");
    }

    function makeSignal() {
        var signal = Object.create(AbortSignal.prototype);
        define(signal, "_aborted", false);
        define(signal, "_reason", undefined);
        define(signal, "_listeners", []);
        define(signal, "_onabort", null);
        return signal;
    }

    function abortSignal(signal, reason) {
        if (signal._aborted) {
            return;
        }
        signal._aborted = true;
        signal._reason = reason === undefined ? domException("signal is aborted without reason", "AbortError") : reason;
        var event = { type: "abort", target: signal, currentTarget: signal };
        var handlers = signal._listeners.slice();
        signal._listeners.length = 0;
        if (typeof signal._onabort === "function") {
            handlers.unshift(signal._onabort);
        }
        handlers.forEach(function (fn) {
            try {
                if (typeof fn === "function") {
                    fn.call(signal, event);
                } else if (fn && typeof fn.handleEvent === "function") {
                    fn.handleEvent(event);
                }
            } catch (e) {
                reportError(e);
            }
        });
    }

    Object.defineProperty(AbortSignal.prototype, "aborted", { get: function () { return this._aborted; }, configurable: true });
    Object.defineProperty(AbortSignal.prototype, "reason", { get: function () { return this._reason; }, configurable: true });
    Object.defineProperty(AbortSignal.prototype, "onabort", {
        get: function () { return this._onabort; },
        set: function (fn) { this._onabort = typeof fn === "function" ? fn : null; },
        configurable: true
    });
    define(AbortSignal.prototype, "throwIfAborted", function () {
        if (this._aborted) {
            throw this._reason;
        }
    });
    define(AbortSignal.prototype, "addEventListener", function (type, fn, options) {
        if (type !== "abort" || fn === null || fn === undefined || this._aborted) {
            return;
        }
        if (this._listeners.indexOf(fn) < 0) {
            if (options && typeof options === "object" && options.once) {
                var self = this;
                var once = function (event) {
                    self.removeEventListener("abort", once);
                    return typeof fn === "function" ? fn.call(self, event) : fn.handleEvent(event);
                };
                this._listeners.push(once);
            } else {
                this._listeners.push(fn);
            }
        }
    });
    define(AbortSignal.prototype, "removeEventListener", function (type, fn) {
        var idx = this._listeners.indexOf(fn);
        if (type === "abort" && idx >= 0) {
            this._listeners.splice(idx, 1);
        }
    });
    define(AbortSignal.prototype, "dispatchEvent", function () {
        return true;
    });
    define(AbortSignal.prototype, Symbol.toStringTag, "AbortSignal");
    define(AbortSignal, "abort", function (reason) {
        var signal = makeSignal();
        abortSignal(signal, reason);
        return signal;
    });
    define(AbortSignal, "timeout", function (ms) {
        var signal = makeSignal();
        global.setTimeout(function () {
            abortSignal(signal, domException("signal timed out", "TimeoutError"));
        }, Math.max(0, Number(ms) || 0));
        return signal;
    });
    define(AbortSignal, "any", function (signals) {
        var signal = makeSignal();
        Array.from(signals).forEach(function (s) {
            if (s.aborted) {
                abortSignal(signal, s.reason);
            } else {
                s.addEventListener("abort", function () {
                    abortSignal(signal, s.reason);
                });
            }
        });
        return signal;
    });

    function AbortController() {
        if (!new.target) {
            throw new TypeError("Constructor AbortController requires 'new'");
        }
        define(this, "_signal", makeSignal());
    }
    Object.defineProperty(AbortController.prototype, "signal", { get: function () { return this._signal; }, configurable: true });
    define(AbortController.prototype, "abort", function (reason) {
        abortSignal(this._signal, reason);
    });
    define(AbortController.prototype, Symbol.toStringTag, "AbortController");

    define(global, "AbortSignal", AbortSignal);
    define(global, "AbortController", AbortController);

    /* Fetch */

    var forbiddenHeaders = {
        "accept-charset": 1, "accept-encoding": 1, "access-control-request-headers": 1,
        "access-control-request-method": 1, "connection": 1, "content-length": 1, "cookie": 1,
        "cookie2": 1, "date": 1, "dnt": 1, "expect": 1, "host": 1, "keep-alive": 1, "origin": 1,
        "referer": 1, "set-cookie": 1, "te": 1, "trailer": 1, "transfer-encoding": 1,
        "upgrade": 1, "via": 1
    };
    var safelistedResponseHeaders = {
        "cache-control": 1, "content-language": 1, "content-length": 1, "content-type": 1,
        "expires": 1, "last-modified": 1, "pragma": 1
    };
    var statusTexts = {
        200: "OK", 201: "Created", 202: "Accepted", 203: "Non-Authoritative Information",
        204: "No Content", 205: "Reset Content", 206: "Partial Content", 300: "Multiple Choices",
        301: "Moved Permanently", 302: "Found", 303: "See Other", 304: "Not Modified",
        307: "Temporary Redirect", 308: "Permanent Redirect", 400: "Bad Request",
        401: "Unauthorized", 403: "Forbidden", 404: "Not Found", 405: "Method Not Allowed",
        406: "Not Acceptable", 408: "Request Timeout", 409: "Conflict", 410: "Gone",
        412: "Precondition Failed", 413: "Content Too Large", 415: "Unsupported Media Type",
        429: "Too Many Requests", 500: "Internal Server Error", 501: "Not Implemented",
        502: "Bad Gateway", 503: "Service Unavailable", 504: "Gateway Timeout"
    };

    function normalizeHeaderName(name) {
        name = String(name);
        if (!/^[!#$%&'*+.^_`|~0-9A-Za-z-]+$/.test(name)) {
            throw new TypeError("'" + name + "' is not a valid HTTP header field name.");
        }
        return name.toLowerCase();
    }

    function normalizeHeaderValue(value) {
        value = String(value).replace(/^[\t\n\r ]+|[\t\n\r ]+$/g, "");
        if (/[\0\r\n]/.test(value)) {
            throw new TypeError("'" + value + "' is not a valid HTTP header field value.");
        }
        return value;
    }

    function Headers(init) {
        if (!new.target) {
            throw new TypeError("Constructor Headers requires 'new'");
        }
        define(this, "_map", new Map());
        define(this, "_guard", "none");
        if (init === undefined || init === null) {
            return;
        }
        var self = this;
        if (init instanceof Headers) {
            init._map.forEach(function (value, name) {
                self._map.set(name, value);
            });
        } else if (typeof init[Symbol.iterator] === "function") {
            Array.from(init).forEach(function (pair) {
                var p = Array.from(pair);
                if (p.length !== 2) {
                    throw new TypeError("Header pairs must contain exactly two items");
                }
                self.append(p[0], p[1]);
            });
        } else if (typeof init === "object") {
            Object.keys(init).forEach(function (key) {
                self.append(key, init[key]);
            });
        } else {
            throw new TypeError("Invalid headers initializer");
        }
    }
    define(Headers.prototype, "append", function (name, value) {
        name = normalizeHeaderName(name);
        value = normalizeHeaderValue(value);
        if (this._guard === "immutable") {
            throw new TypeError("Headers are immutable");
        }
        var old = this._map.get(name);
        this._map.set(name, old === undefined ? value : old + ", " + value);
    });
    define(Headers.prototype, "set", function (name, value) {
        name = normalizeHeaderName(name);
        value = normalizeHeaderValue(value);
        if (this._guard === "immutable") {
            throw new TypeError("Headers are immutable");
        }
        this._map.set(name, value);
    });
    define(Headers.prototype, "get", function (name) {
        var v = this._map.get(normalizeHeaderName(name));
        return v === undefined ? null : v;
    });
    define(Headers.prototype, "has", function (name) {
        return this._map.has(normalizeHeaderName(name));
    });
    define(Headers.prototype, "delete", function (name) {
        if (this._guard === "immutable") {
            throw new TypeError("Headers are immutable");
        }
        this._map.delete(normalizeHeaderName(name));
    });
    define(Headers.prototype, "getSetCookie", function () {
        return [];
    });
    define(Headers.prototype, "entries", function () {
        return Array.from(this._map.entries()).sort(function (a, b) {
            return a[0] < b[0] ? -1 : (a[0] > b[0] ? 1 : 0);
        })[Symbol.iterator]();
    });
    define(Headers.prototype, "keys", function () {
        return Array.from(this.entries(), function (e) { return e[0]; })[Symbol.iterator]();
    });
    define(Headers.prototype, "values", function () {
        return Array.from(this.entries(), function (e) { return e[1]; })[Symbol.iterator]();
    });
    define(Headers.prototype, "forEach", function (fn, thisArg) {
        var self = this;
        Array.from(this.entries()).forEach(function (e) {
            fn.call(thisArg, e[1], e[0], self);
        });
    });
    define(Headers.prototype, Symbol.iterator, Headers.prototype.entries);
    define(Headers.prototype, Symbol.toStringTag, "Headers");

    function bodyToString(body) {
        if (body === undefined || body === null) {
            return null;
        }
        if (body instanceof ArrayBuffer || ArrayBuffer.isView(body)) {
            return native.utf8Decode(body);
        }
        return String(body);
    }

    function defaultContentType(body) {
        if (typeof body === "string" || body instanceof String) {
            return "text/plain;charset=UTF-8";
        }
        return null;
    }

    function consumeBody(holder) {
        if (holder._bodyUsed) {
            return Promise.reject(new TypeError("Body has already been consumed."));
        }
        holder._bodyUsed = true;
        return Promise.resolve(holder._body);
    }

    function bodyMixin(proto) {
        Object.defineProperty(proto, "bodyUsed", { get: function () { return this._bodyUsed; }, configurable: true });
        Object.defineProperty(proto, "body", { get: function () { return null; }, configurable: true });
        define(proto, "arrayBuffer", function () {
            return consumeBody(this).then(function (body) {
                if (body === null) {
                    return new ArrayBuffer(0);
                }
                return body instanceof ArrayBuffer ? body.slice(0) : native.utf8Encode(body);
            });
        });
        define(proto, "bytes", function () {
            return this.arrayBuffer().then(function (buffer) {
                return new Uint8Array(buffer);
            });
        });
        define(proto, "text", function () {
            return consumeBody(this).then(function (body) {
                if (body === null) {
                    return "";
                }
                var text = body instanceof ArrayBuffer ? native.utf8Decode(body) : body;
                return text.charCodeAt(0) === 0xfeff ? text.slice(1) : text;
            });
        });
        define(proto, "json", function () {
            return this.text().then(function (text) {
                return JSON.parse(text);
            });
        });
        define(proto, "blob", function () {
            return Promise.reject(domException("Blob is not supported", "NotSupportedError"));
        });
        define(proto, "formData", function () {
            return Promise.reject(domException("FormData is not supported", "NotSupportedError"));
        });
    }

    function Request(input, init) {
        if (!new.target) {
            throw new TypeError("Constructor Request requires 'new'");
        }
        init = init || {};
        var base = input instanceof Request ? input : null;
        var url = base ? base.url : native.resolve(String(input));
        if (url === null) {
            throw new TypeError("Failed to parse URL from " + input);
        }
        var method = String(init.method !== undefined ? init.method : (base ? base.method : "GET"));
        if (/^(get|head|post|put|delete|options|patch)$/i.test(method)) {
            method = method.toUpperCase();
        }
        var body = init.body !== undefined ? init.body : (base ? base._body : null);
        if ((method === "GET" || method === "HEAD") && body !== null && body !== undefined) {
            throw new TypeError("Request with GET/HEAD method cannot have body.");
        }
        var headers = new Headers(init.headers !== undefined ? init.headers : (base ? base.headers : undefined));
        var bodyText = bodyToString(body);
        var contentType = defaultContentType(body);
        if (bodyText !== null && contentType !== null && !headers.has("content-type")) {
            headers.set("content-type", contentType);
        }
        define(this, "_url", url.replace(/#.*$/, ""));
        define(this, "_method", method);
        define(this, "_headers", headers);
        define(this, "_body", bodyText);
        define(this, "_bodyUsed", false);
        define(this, "_mode", init.mode || (base ? base.mode : "cors"));
        define(this, "_credentials", init.credentials || (base ? base.credentials : "same-origin"));
        define(this, "_redirect", init.redirect || (base ? base.redirect : "follow"));
        define(this, "_signal", init.signal !== undefined ? init.signal : (base ? base.signal : null));
        define(this, "_cache", init.cache || (base ? base.cache : "default"));
        define(this, "_referrer", init.referrer !== undefined ? String(init.referrer) : "about:client");
        if (this._mode === "navigate") {
            throw new TypeError("Cannot construct a Request with a RequestInit whose mode member is set as 'navigate'.");
        }
        if (this._mode === "no-cors" && !/^(GET|HEAD|POST)$/.test(method)) {
            throw new TypeError("'" + method + "' is unsupported in no-cors mode.");
        }
    }
    ["url", "method", "headers", "mode", "credentials", "redirect", "signal", "cache", "referrer"].forEach(function (name) {
        Object.defineProperty(Request.prototype, name, {
            get: function () { return this["_" + name]; },
            configurable: true
        });
    });
    define(Request.prototype, "clone", function () {
        if (this._bodyUsed) {
            throw new TypeError("Request body is already used");
        }
        return new Request(this);
    });
    bodyMixin(Request.prototype);
    define(Request.prototype, Symbol.toStringTag, "Request");

    function Response(body, init) {
        if (!new.target) {
            throw new TypeError("Constructor Response requires 'new'");
        }
        init = init || {};
        var status = init.status === undefined ? 200 : Number(init.status);
        if (!(status >= 200 && status <= 599)) {
            throw new RangeError("The status provided (" + status + ") is outside the range [200, 599].");
        }
        var headers = new Headers(init.headers);
        var bodyValue = body === undefined || body === null ? null :
            (body instanceof ArrayBuffer ? body.slice(0) :
                (ArrayBuffer.isView(body) ? body.buffer.slice(body.byteOffset, body.byteOffset + body.byteLength) : String(body)));
        var contentType = defaultContentType(body);
        if (bodyValue !== null && contentType !== null && !headers.has("content-type")) {
            headers.set("content-type", contentType);
        }
        define(this, "_status", status);
        define(this, "_statusText", init.statusText === undefined ? "" : String(init.statusText));
        define(this, "_headers", headers);
        define(this, "_body", bodyValue);
        define(this, "_bodyUsed", false);
        define(this, "_type", "default");
        define(this, "_url", "");
        define(this, "_redirected", false);
    }
    ["status", "statusText", "headers", "type", "url", "redirected"].forEach(function (name) {
        Object.defineProperty(Response.prototype, name, {
            get: function () { return this["_" + name]; },
            configurable: true
        });
    });
    Object.defineProperty(Response.prototype, "ok", {
        get: function () { return this._status >= 200 && this._status <= 299; },
        configurable: true
    });
    define(Response.prototype, "clone", function () {
        if (this._bodyUsed) {
            throw new TypeError("Response body is already used");
        }
        var copy = Object.create(Response.prototype);
        var self = this;
        ["_status", "_statusText", "_type", "_url", "_redirected"].forEach(function (key) {
            define(copy, key, self[key]);
        });
        define(copy, "_headers", new Headers(this._headers));
        copy._headers._guard = this._headers._guard;
        define(copy, "_body", this._body instanceof ArrayBuffer ? this._body.slice(0) : this._body);
        define(copy, "_bodyUsed", false);
        return copy;
    });
    bodyMixin(Response.prototype);
    define(Response.prototype, Symbol.toStringTag, "Response");
    define(Response, "error", function () {
        var r = new Response(null, { status: 200 });
        r._status = 0;
        r._type = "error";
        r._headers._guard = "immutable";
        return r;
    });
    define(Response, "json", function (data, init) {
        var text = JSON.stringify(data);
        if (text === undefined) {
            throw new TypeError("The data is not JSON serializable");
        }
        var r = new Response(text, init);
        r._headers.set("content-type", "application/json");
        return r;
    });
    define(Response, "redirect", function (url, status) {
        var target = native.resolve(String(url));
        if (target === null) {
            throw new TypeError("Failed to parse URL from " + url);
        }
        status = status === undefined ? 302 : Number(status);
        if ([301, 302, 303, 307, 308].indexOf(status) < 0) {
            throw new RangeError("Invalid status code");
        }
        var r = new Response(null, { status: status });
        r._headers.set("location", target);
        r._headers._guard = "immutable";
        return r;
    });

    function parseHeaderLines(lines) {
        var headers = new Headers();
        lines.forEach(function (line) {
            var idx = line.indexOf(":");
            if (idx > 0) {
                try {
                    headers.append(line.slice(0, idx).trim(), line.slice(idx + 1));
                } catch (e) {
                }
            }
        });
        return headers;
    }

    function isSafelistedRequestHeader(name, value) {
        switch (name) {
        case "accept":
        case "accept-language":
        case "content-language":
            return value.length <= 128;
        case "content-type":
            var essence = value.split(";")[0].trim().toLowerCase();
            return value.length <= 128 && (essence === "application/x-www-form-urlencoded" ||
                essence === "multipart/form-data" || essence === "text/plain");
        }
        return false;
    }

    function schemeOf(url) {
        var m = /^([a-z][a-z0-9+.-]*):/i.exec(url);
        return m ? m[1].toLowerCase() : "";
    }

    function fetch(input, init) {
        var request;
        try {
            request = new Request(input, init);
        } catch (e) {
            return Promise.reject(e);
        }
        return new Promise(function (resolve, reject) {
            var signal = request.signal;
            if (signal && signal.aborted) {
                reject(signal.reason);
                return;
            }
            var docOrigin = native.origin(location.href);
            var url = request.url;
            var method = request.method;
            var body = request._body;
            var redirects = 0;
            var tainted = false;
            var activeId = null;
            var finished = false;

            function fail(error) {
                if (!finished) {
                    finished = true;
                    if (signal) {
                        signal.removeEventListener("abort", onAbort);
                    }
                    reject(error);
                }
            }

            function onAbort() {
                if (activeId !== null) {
                    native.fetchAbort(activeId);
                    activeId = null;
                }
                fail(signal.reason);
            }

            if (signal) {
                signal.addEventListener("abort", onAbort);
            }

            if (method !== "GET" && method !== "HEAD" && method !== "POST") {
                fail(new TypeError("Failed to fetch: method " + method + " is not supported"));
                return;
            }

            function start() {
                var scheme = schemeOf(url);
                var isData = scheme === "data";
                var sameOrigin = isData || (docOrigin !== "null" && native.origin(url) === docOrigin);
                if (!sameOrigin) {
                    tainted = true;
                }
                if (request.mode === "same-origin" && tainted) {
                    fail(new TypeError("Failed to fetch: cross-origin request in same-origin mode"));
                    return;
                }
                if (scheme !== "http" && scheme !== "https" && !isData) {
                    fail(new TypeError("Failed to fetch: unsupported URL scheme"));
                    return;
                }
                var credentials = !isData && (request.credentials === "include" ||
                    (request.credentials === "same-origin" && sameOrigin));
                var lines = [];
                var preflight = false;
                request.headers._map.forEach(function (value, name) {
                    if (name in forbiddenHeaders || /^(proxy-|sec-)/.test(name)) {
                        return;
                    }
                    if (tainted && request.mode === "cors" && !isSafelistedRequestHeader(name, value)) {
                        preflight = true;
                    }
                    lines.push(name + ": " + value);
                });
                if (tainted && !isData) {
                    if (request.mode === "cors" && preflight) {
                        fail(new TypeError("Failed to fetch: CORS preflight requests are not supported"));
                        return;
                    }
                    lines.push("Origin: " + docOrigin);
                } else if (method === "POST" && docOrigin !== "null") {
                    lines.push("Origin: " + docOrigin);
                }
                try {
                    activeId = native.fetch(url, method === "HEAD" ? "GET" : method, lines,
                        method === "POST" ? (body === null ? "" : body) : null,
                        credentials, onResponse);
                } catch (e) {
                    fail(e instanceof TypeError ? e : new TypeError("Failed to fetch"));
                }
            }

            function onResponse(error, status, location, headerLines, buffer) {
                activeId = null;
                if (finished) {
                    return;
                }
                if (error !== null) {
                    fail(new TypeError("Failed to fetch"));
                    return;
                }
                var headers = parseHeaderLines(headerLines);
                if (location !== null && [301, 302, 303, 307, 308].indexOf(status) >= 0) {
                    if (request.redirect === "error") {
                        fail(new TypeError("Failed to fetch: redirect was not allowed"));
                        return;
                    }
                    if (request.redirect === "manual") {
                        var opaque = new Response(null, { status: 200 });
                        opaque._status = 0;
                        opaque._type = "opaqueredirect";
                        opaque._url = url;
                        opaque._headers._guard = "immutable";
                        finish(opaque);
                        return;
                    }
                    if (++redirects > 20) {
                        fail(new TypeError("Failed to fetch: too many redirects"));
                        return;
                    }
                    var next = native.resolve(location, url);
                    if (next === null) {
                        fail(new TypeError("Failed to fetch: bad redirect"));
                        return;
                    }
                    if (status === 303 && method !== "HEAD" || ((status === 301 || status === 302) && method === "POST")) {
                        method = "GET";
                        body = null;
                        request.headers._map.delete("content-type");
                    }
                    url = next.replace(/#.*$/, "");
                    start();
                    return;
                }

                var response = new Response(null, { status: 200 });
                response._status = status;
                response._statusText = statusTexts[status] || "";
                response._url = url;
                response._redirected = redirects > 0;
                response._body = method === "HEAD" || status === 204 || status === 304 ? null : buffer;

                if (tainted && request.mode === "no-cors") {
                    response._status = 0;
                    response._statusText = "";
                    response._type = "opaque";
                    response._url = "";
                    response._body = null;
                    response._headers._guard = "immutable";
                    finish(response);
                    return;
                }
                if (tainted && schemeOf(url) !== "data") {
                    var allowOrigin = headers.get("access-control-allow-origin");
                    var allowCredentials = headers.get("access-control-allow-credentials");
                    var ok = request.credentials === "include"
                        ? allowOrigin === docOrigin && allowCredentials === "true"
                        : allowOrigin === "*" || allowOrigin === docOrigin;
                    if (!ok || docOrigin === "null" && allowOrigin !== "*") {
                        fail(new TypeError("Failed to fetch: blocked by CORS policy"));
                        return;
                    }
                    var exposed = {};
                    (headers.get("access-control-expose-headers") || "").split(",").forEach(function (h) {
                        h = h.trim().toLowerCase();
                        if (h !== "") {
                            exposed[h] = 1;
                        }
                    });
                    headers._map.forEach(function (value, name) {
                        if (!(name in safelistedResponseHeaders) && !(name in exposed) &&
                            !(exposed["*"] && request.credentials !== "include")) {
                            headers._map.delete(name);
                        }
                    });
                    response._type = "cors";
                } else {
                    headers._map.delete("set-cookie");
                    headers._map.delete("set-cookie2");
                    response._type = "basic";
                }
                headers._guard = "immutable";
                response._headers = headers;
                finish(response);
            }

            function finish(response) {
                if (!finished) {
                    finished = true;
                    if (signal) {
                        signal.removeEventListener("abort", onAbort);
                    }
                    resolve(response);
                }
            }

            start();
        });
    }

    define(global, "Headers", Headers);
    define(global, "Request", Request);
    define(global, "Response", Response);
    define(Window.prototype, "fetch", fetch);

    return {
        makeListProxy: function (inner) {
            return new Proxy(inner, {
                has: function (target, key) {
                    var index = listIndex(key);
                    if (index >= 0) {
                        return index < target.length;
                    }
                    return key in target;
                },
                get: function (target, key) {
                    var index = listIndex(key);
                    if (index >= 0) {
                        var item = target.item(index);
                        return item === null ? undefined : item;
                    }
                    return target[key];
                }
            });
        },
        makeNodeMapProxy: function (inner) {
            return new Proxy(inner, {
                has: function (target, key) {
                    var index = listIndex(key);
                    if (index >= 0) {
                        return index < target.length;
                    }
                    if (typeof key === "string" && target.getNamedItem(key)) {
                        return true;
                    }
                    return key in target;
                },
                get: function (target, key) {
                    var index = listIndex(key);
                    if (index >= 0) {
                        var item = target.item(index);
                        return item === null ? undefined : item;
                    }
                    if (typeof key === "string" && !(key in target)) {
                        var attr = target.getNamedItem(key);
                        if (attr) {
                            return attr;
                        }
                    }
                    return target[key];
                }
            });
        },
        consoleFormatter: function () {
            return format(Array.prototype.slice.call(arguments));
        }
    };
})
