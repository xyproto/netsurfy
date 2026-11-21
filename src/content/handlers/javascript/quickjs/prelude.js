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
            return args.map(function (a) {
                if (a instanceof Error && typeof a.stack === "string" && a.stack !== "") {
                    return String(a) + "\n" + a.stack.replace(/\n$/, "");
                }
                return String(a);
            });
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

    var childrenCache = new WeakMap();

    function elementChildren(parent) {
        var out = [];
        for (var n = parent.firstChild; n !== null; n = n.nextSibling) {
            if (n.nodeType === 1) {
                out.push(n);
            }
        }
        return out;
    }

    function namedChild(parent, name) {
        var kids = elementChildren(parent);
        for (var i = 0; i < kids.length; i++) {
            if (name !== "" && (kids[i].id === name || kids[i].getAttribute("name") === name)) {
                return kids[i];
            }
        }
        return null;
    }

    function liveChildren() {
        var self = this;
        var live = childrenCache.get(self);
        if (live !== undefined) {
            return live;
        }
        var methods = {
            item: function (index) {
                var v = elementChildren(self)[index >>> 0];
                return v === undefined ? null : v;
            },
            namedItem: function (name) {
                return namedChild(self, String(name));
            }
        };
        methods[Symbol.iterator] = function () {
            return elementChildren(self)[Symbol.iterator]();
        };
        live = new Proxy(Object.create(HTMLCollection.prototype), {
            get: function (target, key, receiver) {
                if (key === "length") {
                    return elementChildren(self).length;
                }
                var index = listIndex(key);
                if (index >= 0) {
                    return elementChildren(self)[index];
                }
                if (Object.prototype.hasOwnProperty.call(methods, key)) {
                    return methods[key];
                }
                return Reflect.get(target, key, receiver);
            },
            has: function (target, key) {
                var index = listIndex(key);
                if (index >= 0) {
                    return index < elementChildren(self).length;
                }
                return key === "length" || key in target;
            },
            ownKeys: function (target) {
                return elementChildren(self).map(function (_, i) {
                    return String(i);
                }).concat(Reflect.ownKeys(target));
            },
            getOwnPropertyDescriptor: function (target, key) {
                var index = listIndex(key);
                if (index >= 0) {
                    var v = elementChildren(self)[index];
                    return v === undefined ? undefined : { value: v, enumerable: true, configurable: true, writable: false };
                }
                return Reflect.getOwnPropertyDescriptor(target, key);
            }
        });
        childrenCache.set(self, live);
        return live;
    }

    [Element, Document, global.DocumentFragment].forEach(function (iface) {
        if (typeof iface === "function") {
            Object.defineProperty(iface.prototype, "children", {
                get: liveChildren,
                configurable: true,
                enumerable: true
            });
        }
    });

    function accessor(iface, name, get, set) {
        if (typeof iface === "function") {
            Object.defineProperty(iface.prototype, name, {
                get: get,
                set: set,
                configurable: true,
                enumerable: true
            });
        }
    }

    function reflectString(iface, name, attr) {
        accessor(iface, name, function () {
            var v = this.getAttribute(attr);
            return v === null ? "" : v;
        }, function (v) {
            this.setAttribute(attr, String(v));
        });
    }

    function reflectBool(iface, name, attr) {
        accessor(iface, name, function () {
            return this.hasAttribute(attr);
        }, function (v) {
            if (v) {
                this.setAttribute(attr, "");
            } else {
                this.removeAttribute(attr);
            }
        });
    }

    var formControls = [global.HTMLInputElement, global.HTMLTextAreaElement, global.HTMLSelectElement, global.HTMLButtonElement];
    [global.HTMLInputElement, global.HTMLTextAreaElement].forEach(function (iface) {
        reflectString(iface, "placeholder", "placeholder");
        reflectString(iface, "autocomplete", "autocomplete");
        reflectString(iface, "inputMode", "inputmode");
        reflectString(iface, "dirName", "dirname");
    });
    ["pattern", "min", "max", "step", "formAction", "formEnctype", "formMethod", "formTarget"].forEach(function (name) {
        reflectString(global.HTMLInputElement, name, name.toLowerCase());
    });
    reflectBool(global.HTMLInputElement, "multiple", "multiple");
    reflectBool(global.HTMLInputElement, "formNoValidate", "formnovalidate");
    reflectBool(global.HTMLSelectElement, "multiple", "multiple");
    formControls.forEach(function (iface) {
        reflectBool(iface, "required", "required");
        reflectBool(iface, "autofocus", "autofocus");
        accessor(iface, "form", function () {
            var id = this.getAttribute("form");
            if (id !== null) {
                var f = this.isConnected ? this.ownerDocument.getElementById(id) : null;
                return f !== null && f.tagName === "FORM" ? f : null;
            }
            return this.closest("form");
        });
        accessor(iface, "willValidate", function () {
            return !this.disabled && this.type !== "hidden" && this.type !== "button" && this.type !== "reset";
        });
        accessor(iface, "labels", function () {
            var out = [];
            var self = this;
            var labels = this.ownerDocument.getElementsByTagName("label");
            for (var i = 0; i < labels.length; i++) {
                var l = labels[i];
                var f = l.getAttribute("for");
                if ((f !== null && f !== "" && f === self.id) || (f === null && l.contains(self))) {
                    out.push(l);
                }
            }
            return new StaticNodeList(out);
        });
        accessor(iface, "validationMessage", function () {
            return "";
        });
    });
    accessor(global.HTMLInputElement, "indeterminate", function () {
        return this._indeterminate === true;
    }, function (v) {
        define(this, "_indeterminate", !!v);
    });
    [global.HTMLInputElement, global.HTMLTextAreaElement].forEach(function (iface) {
        accessor(iface, "selectionStart", function () {
            return typeof this._selStart === "number" ? Math.min(this._selStart, this.value.length) : this.value.length;
        }, function (v) {
            define(this, "_selStart", v >>> 0);
        });
        accessor(iface, "selectionEnd", function () {
            return typeof this._selEnd === "number" ? Math.min(this._selEnd, this.value.length) : this.value.length;
        }, function (v) {
            define(this, "_selEnd", v >>> 0);
        });
        accessor(iface, "selectionDirection", function () {
            return "none";
        });
    });

    var listedControls = /^(BUTTON|FIELDSET|INPUT|OBJECT|OUTPUT|SELECT|TEXTAREA)$/;
    accessor(global.HTMLFormElement, "elements", function () {
        var form = this;
        var out = [];
        var root = form;
        while (root.parentNode !== null) {
            root = root.parentNode;
        }
        var nodes = root.nodeType === 1 ? [root].concat(Array.prototype.slice.call(root.getElementsByTagName("*"))) :
            root.querySelectorAll("*");
        for (var i = 0; i < nodes.length; i++) {
            var n = nodes[i];
            if (listedControls.test(n.tagName) && !(n.tagName === "INPUT" && (n.getAttribute("type") || "").toLowerCase() === "image") &&
                n.form === form) {
                out.push(n);
            }
        }
        var list = Object.create(HTMLCollection.prototype);
        out.forEach(function (n, i) {
            Object.defineProperty(list, i, { value: n, enumerable: true });
            ["id", "name"].forEach(function (a) {
                var key = n.getAttribute(a);
                if (key && !(key in list)) {
                    Object.defineProperty(list, key, { value: n });
                }
            });
        });
        Object.defineProperty(list, "length", { value: out.length });
        define(list, "item", function (i) {
            return out[i >>> 0] || null;
        });
        define(list, "namedItem", function (name) {
            return Object.prototype.hasOwnProperty.call(list, name) && listIndex(name) < 0 ? list[name] : null;
        });
        define(list, Symbol.iterator, function () {
            return out[Symbol.iterator]();
        });
        return list;
    });
    accessor(global.HTMLFormElement, "length", function () {
        return this.elements.length;
    });
    if (typeof global.HTMLFormElement === "function") {
        define(global.HTMLFormElement.prototype, "requestSubmit", function (submitter) {
            if (submitter !== undefined && submitter !== null && submitter.form !== this) {
                throw domException("The specified element is not owned by this form element", "NotFoundError");
            }
            var novalidate = this.hasAttribute("novalidate") || (submitter && submitter.hasAttribute("formnovalidate"));
            if (!novalidate && !this.checkValidity()) {
                return;
            }
            var evt = typeof SubmitEvent === "function" ?
                new SubmitEvent("submit", { bubbles: true, cancelable: true, submitter: submitter || null }) :
                new Event("submit", { bubbles: true, cancelable: true });
            if (!("submitter" in evt)) {
                define(evt, "submitter", submitter || null);
            }
            if (this.dispatchEvent(evt)) {
                this.submit();
            }
        });
    }

    function childrenByTag(parent, tags) {
        var out = [];
        for (var n = parent.firstChild; n !== null; n = n.nextSibling) {
            if (n.nodeType === 1 && tags.test(n.tagName)) {
                out.push(n);
            }
        }
        return out;
    }

    function arrayCollection(items) {
        var list = Object.create(HTMLCollection.prototype);
        items.forEach(function (n, i) {
            Object.defineProperty(list, i, { value: n, enumerable: true });
        });
        Object.defineProperty(list, "length", { value: items.length });
        define(list, "item", function (i) {
            return items[i >>> 0] || null;
        });
        define(list, "namedItem", function (name) {
            for (var i = 0; i < items.length; i++) {
                if (name !== "" && (items[i].id === name || items[i].getAttribute("name") === name)) {
                    return items[i];
                }
            }
            return null;
        });
        define(list, Symbol.iterator, function () {
            return items[Symbol.iterator]();
        });
        return list;
    }

    function firstChildByTag(parent, tag) {
        var kids = childrenByTag(parent, tag);
        return kids.length > 0 ? kids[0] : null;
    }

    function tableRows(table) {
        var head = [], body = [], foot = [];
        for (var n = table.firstChild; n !== null; n = n.nextSibling) {
            if (n.nodeType !== 1) {
                continue;
            }
            if (n.tagName === "THEAD") {
                head = head.concat(childrenByTag(n, /^TR$/));
            } else if (n.tagName === "TFOOT") {
                foot = foot.concat(childrenByTag(n, /^TR$/));
            } else if (n.tagName === "TBODY") {
                body = body.concat(childrenByTag(n, /^TR$/));
            } else if (n.tagName === "TR") {
                body.push(n);
            }
        }
        return head.concat(body, foot);
    }

    function indexError() {
        return domException("Index out of range", "IndexSizeError");
    }

    var TableElement = global.HTMLTableElement;
    accessor(TableElement, "caption", function () {
        return firstChildByTag(this, /^CAPTION$/);
    });
    accessor(TableElement, "tHead", function () {
        return firstChildByTag(this, /^THEAD$/);
    });
    accessor(TableElement, "tFoot", function () {
        return firstChildByTag(this, /^TFOOT$/);
    });
    accessor(TableElement, "tBodies", function () {
        return arrayCollection(childrenByTag(this, /^TBODY$/));
    });
    accessor(TableElement, "rows", function () {
        return arrayCollection(tableRows(this));
    });
    if (typeof TableElement === "function") {
        define(TableElement.prototype, "createCaption", function () {
            var c = this.caption;
            if (c === null) {
                c = this.ownerDocument.createElement("caption");
                this.insertBefore(c, this.firstChild);
            }
            return c;
        });
        define(TableElement.prototype, "deleteCaption", function () {
            var c = this.caption;
            if (c !== null) {
                this.removeChild(c);
            }
        });
        define(TableElement.prototype, "createTHead", function () {
            var h = this.tHead;
            if (h === null) {
                h = this.ownerDocument.createElement("thead");
                var ref = this.firstChild;
                while (ref !== null && (ref.nodeType !== 1 || /^(CAPTION|COLGROUP)$/.test(ref.tagName))) {
                    ref = ref.nextSibling;
                }
                this.insertBefore(h, ref);
            }
            return h;
        });
        define(TableElement.prototype, "deleteTHead", function () {
            var h = this.tHead;
            if (h !== null) {
                this.removeChild(h);
            }
        });
        define(TableElement.prototype, "createTFoot", function () {
            var f = this.tFoot;
            if (f === null) {
                f = this.ownerDocument.createElement("tfoot");
                this.appendChild(f);
            }
            return f;
        });
        define(TableElement.prototype, "deleteTFoot", function () {
            var f = this.tFoot;
            if (f !== null) {
                this.removeChild(f);
            }
        });
        define(TableElement.prototype, "createTBody", function () {
            var b = this.ownerDocument.createElement("tbody");
            var bodies = childrenByTag(this, /^TBODY$/);
            this.insertBefore(b, bodies.length > 0 ? bodies[bodies.length - 1].nextSibling : null);
            return b;
        });
        define(TableElement.prototype, "insertRow", function (index) {
            index = index === undefined ? -1 : index | 0;
            var rows = tableRows(this);
            if (index < -1 || index > rows.length) {
                throw indexError();
            }
            var tr = this.ownerDocument.createElement("tr");
            if (index === -1 || index === rows.length) {
                var bodies = childrenByTag(this, /^TBODY$/);
                if (rows.length === 0 && bodies.length === 0) {
                    var tbody = this.ownerDocument.createElement("tbody");
                    tbody.appendChild(tr);
                    this.appendChild(tbody);
                } else if (rows.length === 0) {
                    bodies[bodies.length - 1].appendChild(tr);
                } else {
                    rows[rows.length - 1].parentNode.appendChild(tr);
                }
            } else {
                rows[index].parentNode.insertBefore(tr, rows[index]);
            }
            return tr;
        });
        define(TableElement.prototype, "deleteRow", function (index) {
            var rows = tableRows(this);
            index = index | 0;
            if (index === -1) {
                index = rows.length - 1;
                if (index < 0) {
                    return;
                }
            }
            if (index < 0 || index >= rows.length) {
                throw indexError();
            }
            rows[index].parentNode.removeChild(rows[index]);
        });
    }

    var SectionElement = global.HTMLTableSectionElement;
    accessor(SectionElement, "rows", function () {
        return arrayCollection(childrenByTag(this, /^TR$/));
    });
    if (typeof SectionElement === "function") {
        define(SectionElement.prototype, "insertRow", function (index) {
            index = index === undefined ? -1 : index | 0;
            var rows = childrenByTag(this, /^TR$/);
            if (index < -1 || index > rows.length) {
                throw indexError();
            }
            var tr = this.ownerDocument.createElement("tr");
            this.insertBefore(tr, index === -1 || index === rows.length ? null : rows[index]);
            return tr;
        });
        define(SectionElement.prototype, "deleteRow", function (index) {
            var rows = childrenByTag(this, /^TR$/);
            index = index | 0;
            if (index === -1) {
                index = rows.length - 1;
                if (index < 0) {
                    return;
                }
            }
            if (index < 0 || index >= rows.length) {
                throw indexError();
            }
            this.removeChild(rows[index]);
        });
    }

    var RowElement = global.HTMLTableRowElement;
    accessor(RowElement, "rowIndex", function () {
        var p = this.parentNode;
        var table = p === null ? null : p.tagName === "TABLE" ? p : p.parentNode;
        if (table === null || table.tagName !== "TABLE") {
            return -1;
        }
        return tableRows(table).indexOf(this);
    });
    accessor(RowElement, "sectionRowIndex", function () {
        var p = this.parentNode;
        return p === null ? -1 : childrenByTag(p, /^TR$/).indexOf(this);
    });
    accessor(RowElement, "cells", function () {
        return arrayCollection(childrenByTag(this, /^T[DH]$/));
    });
    if (typeof RowElement === "function") {
        define(RowElement.prototype, "insertCell", function (index) {
            index = index === undefined ? -1 : index | 0;
            var cells = childrenByTag(this, /^T[DH]$/);
            if (index < -1 || index > cells.length) {
                throw indexError();
            }
            var td = this.ownerDocument.createElement("td");
            this.insertBefore(td, index === -1 || index === cells.length ? null : cells[index]);
            return td;
        });
        define(RowElement.prototype, "deleteCell", function (index) {
            var cells = childrenByTag(this, /^T[DH]$/);
            index = index | 0;
            if (index === -1) {
                index = cells.length - 1;
                if (index < 0) {
                    return;
                }
            }
            if (index < 0 || index >= cells.length) {
                throw indexError();
            }
            this.removeChild(cells[index]);
        });
    }

    var inputTypes = /^(hidden|text|search|tel|url|email|password|date|month|week|time|datetime-local|number|range|color|checkbox|radio|file|submit|image|reset|button)$/;
    accessor(global.HTMLInputElement, "type", function () {
        var t = (this.getAttribute("type") || "").toLowerCase();
        return inputTypes.test(t) ? t : "text";
    }, function (v) {
        this.setAttribute("type", String(v));
    });
    accessor(global.HTMLButtonElement, "type", function () {
        var t = (this.getAttribute("type") || "").toLowerCase();
        return t === "reset" || t === "button" ? t : "submit";
    }, function (v) {
        this.setAttribute("type", String(v));
    });
    reflectString(global.HTMLButtonElement, "value", "value");

    (function () {
        var Input = global.HTMLInputElement;
        var desc = Input && Object.getOwnPropertyDescriptor(Input.prototype, "checked");
        if (!desc || typeof desc.set !== "function") {
            return;
        }
        Object.defineProperty(Input.prototype, "checked", {
            get: desc.get,
            set: function (v) {
                desc.set.call(this, v);
                if (!v || this.type !== "radio" || this.name === "") {
                    return;
                }
                var form = this.form;
                var scope = form !== null ? form : this.ownerDocument;
                var radios = scope.getElementsByTagName("input");
                for (var i = 0; i < radios.length; i++) {
                    var r = radios[i];
                    if (r !== this && r.type === "radio" && r.name === this.name && r.form === form &&
                        desc.get.call(r)) {
                        desc.set.call(r, false);
                    }
                }
            },
            configurable: true,
            enumerable: true
        });
    })();

    function selectOptions(select) {
        var out = [];
        for (var n = select.firstChild; n !== null; n = n.nextSibling) {
            if (n.nodeType !== 1) {
                continue;
            }
            if (n.tagName === "OPTION") {
                out.push(n);
            } else if (n.tagName === "OPTGROUP") {
                out = out.concat(childrenByTag(n, /^OPTION$/));
            }
        }
        return out;
    }

    function setSelectLength(select, len) {
        var opts = selectOptions(select);
        len = len >>> 0;
        while (opts.length > len) {
            var o = opts.pop();
            o.parentNode.removeChild(o);
        }
        while (opts.length < len) {
            opts.push(select.appendChild(select.ownerDocument.createElement("option")));
        }
    }

    var optionsCache = new WeakMap();
    accessor(global.HTMLSelectElement, "options", function () {
        var select = this;
        var live = optionsCache.get(select);
        if (live !== undefined) {
            return live;
        }
        var methods = {
            item: function (i) {
                return selectOptions(select)[i >>> 0] || null;
            },
            namedItem: function (name) {
                var opts = selectOptions(select);
                for (var i = 0; i < opts.length; i++) {
                    if (name !== "" && (opts[i].id === name || opts[i].getAttribute("name") === name)) {
                        return opts[i];
                    }
                }
                return null;
            },
            add: function (element, before) {
                return select.add(element, before);
            },
            remove: function (index) {
                return select.remove(index);
            }
        };
        methods[Symbol.iterator] = function () {
            return selectOptions(select)[Symbol.iterator]();
        };
        live = new Proxy(Object.create(HTMLCollection.prototype), {
            get: function (target, key, receiver) {
                if (key === "length") {
                    return selectOptions(select).length;
                }
                if (key === "selectedIndex") {
                    return select.selectedIndex;
                }
                var index = listIndex(key);
                if (index >= 0) {
                    return selectOptions(select)[index];
                }
                if (Object.prototype.hasOwnProperty.call(methods, key)) {
                    return methods[key];
                }
                return Reflect.get(target, key, receiver);
            },
            set: function (target, key, value) {
                if (key === "length") {
                    setSelectLength(select, value);
                    return true;
                }
                if (key === "selectedIndex") {
                    select.selectedIndex = value;
                    return true;
                }
                var index = listIndex(key);
                if (index >= 0) {
                    var opts = selectOptions(select);
                    if (value === null || value === undefined) {
                        if (index < opts.length) {
                            opts[index].parentNode.removeChild(opts[index]);
                        }
                    } else if (index < opts.length) {
                        opts[index].parentNode.replaceChild(value, opts[index]);
                    } else {
                        setSelectLength(select, index);
                        select.appendChild(value);
                    }
                    return true;
                }
                target[key] = value;
                return true;
            },
            has: function (target, key) {
                var index = listIndex(key);
                if (index >= 0) {
                    return index < selectOptions(select).length;
                }
                return key === "length" || key in target;
            }
        });
        optionsCache.set(select, live);
        return live;
    });
    accessor(global.HTMLSelectElement, "length", function () {
        return selectOptions(this).length;
    }, function (v) {
        setSelectLength(this, v);
    });
    accessor(global.HTMLSelectElement, "selectedIndex", function () {
        var opts = selectOptions(this);
        for (var i = 0; i < opts.length; i++) {
            if (opts[i].selected) {
                return i;
            }
        }
        return -1;
    }, function (v) {
        var opts = selectOptions(this);
        v = v | 0;
        for (var i = 0; i < opts.length; i++) {
            opts[i].selected = i === v;
        }
    });
    accessor(global.HTMLSelectElement, "selectedOptions", function () {
        return arrayCollection(selectOptions(this).filter(function (o) {
            return o.selected;
        }));
    });
    accessor(global.HTMLOptionElement, "index", function () {
        var p = this.parentNode;
        if (p !== null && p.tagName === "OPTGROUP") {
            p = p.parentNode;
        }
        return p !== null && p.tagName === "SELECT" ? selectOptions(p).indexOf(this) : 0;
    });
    accessor(global.HTMLOptionElement, "form", function () {
        var p = this.closest("select");
        return p === null ? null : p.form;
    });

    if (typeof global.HTMLSelectElement === "function") {
        define(global.HTMLSelectElement.prototype, "item", function (i) {
            return selectOptions(this)[i >>> 0] || null;
        });
        define(global.HTMLSelectElement.prototype, "namedItem", function (name) {
            return this.options.namedItem(name);
        });
        define(global.HTMLSelectElement.prototype, "remove", function (index) {
            if (arguments.length === 0) {
                if (this.parentNode !== null) {
                    this.parentNode.removeChild(this);
                }
                return;
            }
            var o = selectOptions(this)[index | 0];
            if (o !== undefined) {
                o.parentNode.removeChild(o);
            }
        });
        define(global.HTMLSelectElement.prototype, "add", function (element, before) {
            var ref = null;
            if (typeof before === "number") {
                ref = this.options[before] || null;
            } else if (before !== undefined && before !== null) {
                ref = before;
            }
            if (ref !== null && ref.parentNode !== null) {
                ref.parentNode.insertBefore(element, ref);
            } else {
                this.appendChild(element);
            }
        });
    }

    reflectString(global.HTMLAnchorElement, "type", "type");
    reflectString(global.HTMLAnchorElement, "download", "download");
    reflectString(global.HTMLAnchorElement, "ping", "ping");
    accessor(global.HTMLAnchorElement, "text", function () {
        return this.textContent;
    }, function (v) {
        this.textContent = v;
    });

    var focusableTags = /^(A|AREA|BUTTON|INPUT|SELECT|TEXTAREA|IFRAME|SUMMARY)$/;
    accessor(HTMLElement, "tabIndex", function () {
        var v = parseInt(this.getAttribute("tabindex"), 10);
        if (!isNaN(v)) {
            return v;
        }
        return focusableTags.test(this.tagName) ? 0 : -1;
    }, function (v) {
        this.setAttribute("tabindex", String(v | 0));
    });
    accessor(HTMLElement, "draggable", function () {
        var v = this.getAttribute("draggable");
        return v === "true" || (v === null && (this.tagName === "IMG" || (this.tagName === "A" && this.hasAttribute("href"))));
    }, function (v) {
        this.setAttribute("draggable", v ? "true" : "false");
    });
    accessor(HTMLElement, "spellcheck", function () {
        return this.getAttribute("spellcheck") !== "false";
    }, function (v) {
        this.setAttribute("spellcheck", v ? "true" : "false");
    });
    accessor(HTMLElement, "contentEditable", function () {
        var v = this.getAttribute("contenteditable");
        if (v === null) {
            return "inherit";
        }
        v = v.toLowerCase();
        return v === "" || v === "true" ? "true" : v === "false" ? "false" : v === "plaintext-only" ? v : "inherit";
    }, function (v) {
        v = String(v).toLowerCase();
        if (v === "inherit") {
            this.removeAttribute("contenteditable");
        } else {
            this.setAttribute("contenteditable", v);
        }
    });
    accessor(HTMLElement, "isContentEditable", function () {
        for (var n = this; n !== null && n.nodeType === 1; n = n.parentNode) {
            var v = n.contentEditable;
            if (v !== "inherit") {
                return v !== "false";
            }
        }
        return false;
    });

    accessor(Document, "URL", function () {
        return this.location ? this.location.href : "about:blank";
    });
    accessor(Document, "documentURI", function () {
        return this.URL;
    });
    accessor(Document, "domain", function () {
        return this.location ? this.location.hostname : "";
    }, function () {
    });
    accessor(Document, "dir", function () {
        var el = this.documentElement;
        return el ? el.dir : "";
    }, function (v) {
        if (this.documentElement) {
            this.documentElement.dir = v;
        }
    });
    accessor(Document, "lastModified", function () {
        var d = new Date();
        function two(n) {
            return (n < 10 ? "0" : "") + n;
        }
        return two(d.getMonth() + 1) + "/" + two(d.getDate()) + "/" + d.getFullYear() + " " +
            two(d.getHours()) + ":" + two(d.getMinutes()) + ":" + two(d.getSeconds());
    });
    [Document, global.DocumentFragment].forEach(function (iface) {
        accessor(iface, "firstElementChild", function () {
            var kids = elementChildren(this);
            return kids.length > 0 ? kids[0] : null;
        });
        accessor(iface, "lastElementChild", function () {
            var kids = elementChildren(this);
            return kids.length > 0 ? kids[kids.length - 1] : null;
        });
        accessor(iface, "childElementCount", function () {
            return elementChildren(this).length;
        });
    });

    var nameStartChars = ":A-Z_a-z\u00C0-\u00D6\u00D8-\u00F6\u00F8-\u02FF\u0370-\u037D\u037F-\u1FFF\u200C\u200D\u2070-\u218F\u2C00-\u2FEF\u3001-\uD7FF\uF900-\uFDCF\uFDF0-\uFFFD\u{10000}-\u{EFFFF}";
    var nameChars = nameStartChars + "\\-.0-9\u00B7\u0300-\u036F\u203F\u2040";
    var xmlName = new RegExp("^[" + nameStartChars + "][" + nameChars + "]*$", "u");
    var xmlNs = "http://www.w3.org/XML/1998/namespace";
    var xmlnsNs = "http://www.w3.org/2000/xmlns/";

    function validateName(name) {
        if (!xmlName.test(name)) {
            throw domException("String contains an invalid character", "InvalidCharacterError");
        }
    }

    function namespaceError() {
        return domException("The qualified name is not valid in this namespace", "NamespaceError");
    }

    function splitQName(name) {
        validateName(name);
        var parts = name.split(":");
        if (parts.length > 2 || parts[0] === "" || parts[parts.length - 1] === "") {
            throw namespaceError();
        }
        return parts.length === 2 ? parts : [null, name];
    }

    function validateQName(ns, name) {
        var prefix = splitQName(name)[0];
        if ((prefix !== null && ns === null) ||
            (prefix === "xml" && ns !== xmlNs) ||
            ((prefix === "xmlns" || name === "xmlns") !== (ns === xmlnsNs))) {
            throw namespaceError();
        }
    }

    var nativeCreateDocumentType = DOMImplementation.prototype.createDocumentType;
    define(DOMImplementation.prototype, "createDocumentType", function (name) {
        splitQName(String(name));
        return nativeCreateDocumentType.apply(this, arguments);
    });

    [["createElement", 0], ["createElementNS", 1]].forEach(function (entry) {
        var orig = Document.prototype[entry[0]];
        define(Document.prototype, entry[0], function (a, b) {
            if (entry[1] === 0) {
                validateName(String(a));
            } else {
                validateQName(a === undefined || a === null || a === "" ? null : String(a), String(b));
            }
            return orig.apply(this, arguments);
        });
    });

    var eventStamps = new WeakMap();
    accessor(Event, "timeStamp", function () {
        var t = eventStamps.get(this);
        if (t === undefined) {
            t = typeof performance === "object" ? performance.now() : Date.now();
            eventStamps.set(this, t);
        }
        return t;
    });

    accessor(Window, "opener", function () {
        return null;
    }, function () {
    });
    accessor(Window, "frameElement", function () {
        return null;
    });
    var windowStatus = "";
    accessor(Window, "status", function () {
        return windowStatus;
    }, function (v) {
        windowStatus = String(v);
    });

    function splitRules(text) {
        var rules = [];
        var depth = 0;
        var start = 0;
        var quote = null;
        for (var i = 0; i < text.length; i++) {
            var c = text[i];
            if (quote !== null) {
                if (c === "\\") {
                    i++;
                } else if (c === quote) {
                    quote = null;
                }
            } else if (c === "/" && text[i + 1] === "*") {
                var end = text.indexOf("*/", i + 2);
                i = end < 0 ? text.length : end + 1;
            } else if (c === "\"" || c === "'") {
                quote = c;
            } else if (c === "{") {
                depth++;
            } else if (c === "}") {
                depth--;
                if (depth <= 0) {
                    depth = 0;
                    rules.push(text.slice(start, i + 1).replace(/^(\s|\/\*[^]*?\*\/)+/, ""));
                    start = i + 1;
                }
            } else if (c === ";" && depth === 0) {
                rules.push(text.slice(start, i + 1).replace(/^(\s|\/\*[^]*?\*\/)+/, ""));
                start = i + 1;
            }
        }
        return rules.filter(function (r) {
            return r.trim() !== "";
        });
    }

    function CSSRule(sheet, text) {
        define(this, "parentStyleSheet", sheet);
        define(this, "cssText", text);
        var brace = text.indexOf("{");
        define(this, "selectorText", brace >= 0 && text[0] !== "@" ? text.slice(0, brace).trim() : "");
        define(this, "type", text[0] === "@" ? (/^@media/i.test(text) ? 4 : /^@import/i.test(text) ? 3 : /^@font-face/i.test(text) ? 5 : /^@keyframes|^@-\w+-keyframes/i.test(text) ? 7 : 0) : 1);
    }

    function CSSStyleSheet(owner) {
        define(this, "ownerNode", owner);
        define(this, "_rules", null);
        define(this, "_text", null);
    }
    CSSStyleSheet.prototype = {
        constructor: CSSStyleSheet,
        type: "text/css",
        disabled: false,
        parentStyleSheet: null,
        ownerRule: null,
        get href() {
            return this.ownerNode && this.ownerNode.tagName === "LINK" ? this.ownerNode.href : null;
        },
        get title() {
            return this.ownerNode ? this.ownerNode.getAttribute("title") : null;
        },
        get media() {
            var m = this.ownerNode ? this.ownerNode.getAttribute("media") || "" : "";
            return { mediaText: m, length: m === "" ? 0 : m.split(",").length };
        },
        _sync: function () {
            var text = this.ownerNode && this.ownerNode.tagName === "STYLE" ? this.ownerNode.textContent || "" : "";
            if (this._rules === null || text !== this._text) {
                var self = this;
                this._text = text;
                this._rules = splitRules(text).map(function (r) {
                    return new CSSRule(self, r);
                });
            }
            return this._rules;
        },
        _write: function () {
            var text = this._rules.map(function (r) {
                return r.cssText;
            }).join("\n");
            this._text = text;
            if (this.ownerNode && this.ownerNode.tagName === "STYLE") {
                this.ownerNode.textContent = text;
                this._text = this.ownerNode.textContent || "";
            }
        },
        get cssRules() {
            return this._sync().slice();
        },
        get rules() {
            return this.cssRules;
        },
        insertRule: function (rule, index) {
            var rules = this._sync();
            index = index === undefined ? 0 : index >>> 0;
            if (index > rules.length) {
                throw domException("Index out of range", "IndexSizeError");
            }
            var parsed = splitRules(String(rule));
            if (parsed.length !== 1) {
                throw domException("Failed to parse the rule '" + rule + "'", "SyntaxError");
            }
            rules.splice(index, 0, new CSSRule(this, parsed[0]));
            this._write();
            return index;
        },
        deleteRule: function (index) {
            var rules = this._sync();
            index = index >>> 0;
            if (index >= rules.length) {
                throw domException("Index out of range", "IndexSizeError");
            }
            rules.splice(index, 1);
            this._write();
        },
        addRule: function (selector, style, index) {
            return this.insertRule(selector + " { " + style + " }", index === undefined ? this._sync().length : index);
        },
        removeRule: function (index) {
            this.deleteRule(index === undefined ? 0 : index);
        },
        replaceSync: function (text) {
            define(this, "_rules", splitRules(String(text)).map(function (r) {
                return new CSSRule(this, r);
            }, this));
            this._write();
        },
        replace: function (text) {
            this.replaceSync(text);
            return Promise.resolve(this);
        }
    };
    if (typeof global.CSSStyleSheet !== "function") {
        define(global, "CSSStyleSheet", CSSStyleSheet);
        define(global, "StyleSheet", CSSStyleSheet);
        define(global, "CSSRule", CSSRule);
    }

    var sheetCache = new WeakMap();
    function sheetOf() {
        var sheet = sheetCache.get(this);
        if (sheet === undefined) {
            sheet = new CSSStyleSheet(this);
            sheetCache.set(this, sheet);
        }
        if (this.tagName === "LINK" && !/(^|\s)stylesheet(\s|$)/i.test(this.getAttribute("rel") || "")) {
            return null;
        }
        return this.isConnected === false ? null : sheet;
    }
    accessor(global.HTMLStyleElement, "sheet", sheetOf);
    accessor(global.HTMLLinkElement, "sheet", sheetOf);
    accessor(Document, "styleSheets", function () {
        var out = [];
        var nodes = this.querySelectorAll("style, link");
        for (var i = 0; i < nodes.length; i++) {
            var sheet = nodes[i].sheet;
            if (sheet) {
                out.push(sheet);
            }
        }
        out.item = function (i) {
            return this[i] || null;
        };
        return out;
    });

    var NodeFilterConstants = {
        FILTER_ACCEPT: 1, FILTER_REJECT: 2, FILTER_SKIP: 3,
        SHOW_ALL: 0xFFFFFFFF, SHOW_ELEMENT: 0x1, SHOW_ATTRIBUTE: 0x2, SHOW_TEXT: 0x4,
        SHOW_CDATA_SECTION: 0x8, SHOW_ENTITY_REFERENCE: 0x10, SHOW_ENTITY: 0x20,
        SHOW_PROCESSING_INSTRUCTION: 0x40, SHOW_COMMENT: 0x80, SHOW_DOCUMENT: 0x100,
        SHOW_DOCUMENT_TYPE: 0x200, SHOW_DOCUMENT_FRAGMENT: 0x400, SHOW_NOTATION: 0x800
    };
    var NodeFilter = {};
    Object.keys(NodeFilterConstants).forEach(function (k) {
        Object.defineProperty(NodeFilter, k, { value: NodeFilterConstants[k], enumerable: true });
    });
    define(global, "NodeFilter", NodeFilter);

    function traversalFilter(t, node) {
        if (t._active) {
            throw domException("Recursive filter invocation", "InvalidStateError");
        }
        if (!(t.whatToShow & (1 << (node.nodeType - 1)))) {
            return 3;
        }
        var f = t.filter;
        if (f === null || f === undefined) {
            return 1;
        }
        t._active = true;
        try {
            var r = typeof f === "function" ? f.call(undefined, node) : f.acceptNode(node);
            return Number(r);
        } finally {
            t._active = false;
        }
    }

    function followingIn(node, root, skipChildren) {
        if (!skipChildren && node.firstChild !== null) {
            return node.firstChild;
        }
        for (var n = node; n !== null && n !== root; n = n.parentNode) {
            if (n.nextSibling !== null) {
                return n.nextSibling;
            }
        }
        return null;
    }

    function lastInclusiveDescendant(node) {
        while (node.lastChild !== null) {
            node = node.lastChild;
        }
        return node;
    }

    function isInclusiveAncestor(a, b) {
        for (var n = b; n !== null; n = n.parentNode) {
            if (n === a) {
                return true;
            }
        }
        return false;
    }

    var liveIterators = [];

    function iteratorPreRemove(node) {
        for (var i = liveIterators.length - 1; i >= 0; i--) {
            var it = liveIterators[i].deref();
            if (it === undefined) {
                liveIterators.splice(i, 1);
                continue;
            }
            if (node === it.root || !isInclusiveAncestor(node, it._ref) ||
                !isInclusiveAncestor(it.root, node)) {
                continue;
            }
            if (it._before) {
                var next = followingIn(node, it.root, true);
                if (next !== null && isInclusiveAncestor(it.root, next)) {
                    it._ref = next;
                    continue;
                }
                it._before = false;
            }
            it._ref = node.previousSibling !== null ? lastInclusiveDescendant(node.previousSibling) : node.parentNode;
        }
    }

    ["removeChild", "replaceChild", "insertBefore", "appendChild"].forEach(function (name) {
        var orig = Node.prototype[name];
        if (typeof orig !== "function") {
            return;
        }
        define(Node.prototype, name, function (a, b) {
            if (liveIterators.length > 0) {
                var moving = name === "removeChild" ? a : name === "replaceChild" ? b : a;
                if (name === "replaceChild" && a && a.parentNode !== null) {
                    iteratorPreRemove(a);
                }
                if (moving && moving.parentNode !== null) {
                    iteratorPreRemove(moving);
                }
            }
            return orig.apply(this, arguments);
        });
    });

    function NodeIterator() {
        throw new TypeError("Illegal constructor");
    }
    NodeIterator.prototype = {
        constructor: NodeIterator,
        get referenceNode() {
            return this._ref;
        },
        get pointerBeforeReferenceNode() {
            return this._before;
        },
        _traverse: function (next) {
            var savedRef = this._ref;
            var savedBefore = this._before;
            for (;;) {
                var node = this._ref;
                if (next) {
                    if (!this._before) {
                        node = followingIn(node, this.root, false);
                        if (node === null) {
                            this._ref = savedRef;
                            this._before = savedBefore;
                            return null;
                        }
                    } else {
                        this._before = false;
                    }
                } else {
                    if (this._before) {
                        if (node === this.root) {
                            this._ref = savedRef;
                            this._before = savedBefore;
                            return null;
                        }
                        node = node.previousSibling !== null ? lastInclusiveDescendant(node.previousSibling) : node.parentNode;
                        if (node === null) {
                            this._ref = savedRef;
                            this._before = savedBefore;
                            return null;
                        }
                    } else {
                        this._before = true;
                    }
                }
                this._ref = node;
                var result;
                try {
                    result = traversalFilter(this, node);
                } catch (e) {
                    this._ref = savedRef;
                    this._before = savedBefore;
                    throw e;
                }
                if (result === 1) {
                    return node;
                }
            }
        },
        nextNode: function () {
            return this._traverse(true);
        },
        previousNode: function () {
            return this._traverse(false);
        },
        detach: function () {
        },
        toString: function () {
            return "[object NodeIterator]";
        }
    };
    define(global, "NodeIterator", NodeIterator);

    function TreeWalker() {
        throw new TypeError("Illegal constructor");
    }
    TreeWalker.prototype = {
        constructor: TreeWalker,
        get currentNode() {
            return this._current;
        },
        set currentNode(node) {
            if (!(node instanceof Node)) {
                throw new TypeError("parameter 1 is not of type 'Node'");
            }
            this._current = node;
        },
        parentNode: function () {
            var node = this._current;
            while (node !== null && node !== this.root) {
                node = node.parentNode;
                if (node !== null && traversalFilter(this, node) === 1) {
                    this._current = node;
                    return node;
                }
            }
            return null;
        },
        _children: function (first) {
            var node = first ? this._current.firstChild : this._current.lastChild;
            while (node !== null) {
                var result = traversalFilter(this, node);
                if (result === 1) {
                    this._current = node;
                    return node;
                }
                if (result === 3) {
                    var child = first ? node.firstChild : node.lastChild;
                    if (child !== null) {
                        node = child;
                        continue;
                    }
                }
                while (node !== null) {
                    var sibling = first ? node.nextSibling : node.previousSibling;
                    if (sibling !== null) {
                        node = sibling;
                        break;
                    }
                    var parent = node.parentNode;
                    if (parent === null || parent === this.root || parent === this._current) {
                        return null;
                    }
                    node = parent;
                }
            }
            return null;
        },
        firstChild: function () {
            return this._children(true);
        },
        lastChild: function () {
            return this._children(false);
        },
        _siblings: function (next) {
            var node = this._current;
            if (node === this.root) {
                return null;
            }
            for (;;) {
                var sibling = next ? node.nextSibling : node.previousSibling;
                while (sibling !== null) {
                    node = sibling;
                    var result = traversalFilter(this, node);
                    if (result === 1) {
                        this._current = node;
                        return node;
                    }
                    sibling = next ? node.firstChild : node.lastChild;
                    if (result === 2 || sibling === null) {
                        sibling = next ? node.nextSibling : node.previousSibling;
                    }
                }
                node = node.parentNode;
                if (node === null || node === this.root) {
                    return null;
                }
                if (traversalFilter(this, node) === 1) {
                    return null;
                }
            }
        },
        nextSibling: function () {
            return this._siblings(true);
        },
        previousSibling: function () {
            return this._siblings(false);
        },
        previousNode: function () {
            var node = this._current;
            while (node !== this.root) {
                var sibling = node.previousSibling;
                while (sibling !== null) {
                    node = sibling;
                    var result = traversalFilter(this, node);
                    while (result !== 2 && node.lastChild !== null) {
                        node = node.lastChild;
                        result = traversalFilter(this, node);
                    }
                    if (result === 1) {
                        this._current = node;
                        return node;
                    }
                    sibling = node.previousSibling;
                }
                if (node === this.root || node.parentNode === null) {
                    return null;
                }
                node = node.parentNode;
                if (traversalFilter(this, node) === 1) {
                    this._current = node;
                    return node;
                }
            }
            return null;
        },
        nextNode: function () {
            var node = this._current;
            var result = 1;
            for (;;) {
                while (result !== 2 && node.firstChild !== null) {
                    node = node.firstChild;
                    result = traversalFilter(this, node);
                    if (result === 1) {
                        this._current = node;
                        return node;
                    }
                }
                var sibling = null;
                for (var temp = node; temp !== null; temp = temp.parentNode) {
                    if (temp === this.root) {
                        return null;
                    }
                    sibling = temp.nextSibling;
                    if (sibling !== null) {
                        node = sibling;
                        break;
                    }
                }
                if (sibling === null) {
                    return null;
                }
                result = traversalFilter(this, node);
                if (result === 1) {
                    this._current = node;
                    return node;
                }
            }
        },
        toString: function () {
            return "[object TreeWalker]";
        }
    };
    define(global, "TreeWalker", TreeWalker);

    function makeTraversal(proto, root, whatToShow, filter) {
        if (!(root instanceof Node)) {
            throw new TypeError("parameter 1 is not of type 'Node'");
        }
        var t = Object.create(proto);
        define(t, "root", root);
        define(t, "whatToShow", whatToShow === undefined ? 0xFFFFFFFF : whatToShow >>> 0);
        define(t, "filter", filter === undefined ? null : filter);
        define(t, "_active", false);
        return t;
    }

    define(Document.prototype, "createNodeIterator", function (root, whatToShow, filter) {
        var it = makeTraversal(NodeIterator.prototype, root, whatToShow, filter);
        define(it, "_ref", root);
        define(it, "_before", true);
        liveIterators.push(new WeakRef(it));
        return it;
    });
    define(Document.prototype, "createTreeWalker", function (root, whatToShow, filter) {
        var w = makeTraversal(TreeWalker.prototype, root, whatToShow, filter);
        define(w, "_current", root);
        return w;
    });

    var comparePoints = native.comparePoints;

    function isCharacterData(node) {
        var t = node.nodeType;
        return t === 3 || t === 4 || t === 7 || t === 8;
    }

    function nodeLength(node) {
        if (node.nodeType === 10) {
            return 0;
        }
        return isCharacterData(node) ? node.length : node.childNodes.length;
    }

    function nodeIndex(node) {
        var index = 0;
        while ((node = node.previousSibling) !== null) {
            index++;
        }
        return index;
    }

    function rootOf(node) {
        while (node.parentNode !== null) {
            node = node.parentNode;
        }
        return node;
    }

    function requireNode(node) {
        if (!(node instanceof Node)) {
            throw new TypeError("parameter 1 is not of type 'Node'");
        }
        return node;
    }

    function parentOrThrow(node) {
        var parent = requireNode(node).parentNode;
        if (parent === null) {
            throw domException("The node has no parent", "InvalidNodeTypeError");
        }
        return parent;
    }

    function rangeContainsNode(range, node) {
        return comparePoints(node, 0, range.startContainer, range.startOffset) > 0 &&
            comparePoints(node, nodeLength(node), range.endContainer, range.endOffset) < 0;
    }

    function containedNodes(range, includeDescendants) {
        var out = [];
        var common = range.commonAncestorContainer;
        var n = common.firstChild;
        while (n !== null) {
            var inside = rangeContainsNode(range, n);
            if (inside) {
                out.push(n);
            }
            n = followingIn(n, common, inside && !includeDescendants);
        }
        return out;
    }

    function cutPoint(sc, so, ec) {
        if (isInclusiveAncestor(sc, ec)) {
            return [sc, so];
        }
        var ref = sc;
        while (!isInclusiveAncestor(ref.parentNode, ec)) {
            ref = ref.parentNode;
        }
        return [ref.parentNode, nodeIndex(ref) + 1];
    }

    function cloneData(node, offset, count) {
        var c = node.cloneNode(false);
        c.data = node.substringData(offset, count);
        return c;
    }

    function rangeContents(range, extract) {
        var sc = range.startContainer, so = range.startOffset;
        var ec = range.endContainer, eo = range.endOffset;
        var doc = sc.nodeType === 9 ? sc : sc.ownerDocument;
        var frag = doc.createDocumentFragment();
        if (sc === ec && so === eo) {
            return frag;
        }
        if (sc === ec && isCharacterData(sc)) {
            frag.appendChild(cloneData(sc, so, eo - so));
            if (extract) {
                sc.replaceData(so, eo - so, "");
            }
            return frag;
        }
        var common = range.commonAncestorContainer;
        var first = null, last = null, contained = [];
        for (var n = common.firstChild; n !== null; n = n.nextSibling) {
            var hasStart = isInclusiveAncestor(n, sc), hasEnd = isInclusiveAncestor(n, ec);
            if (hasStart && !hasEnd) {
                first = n;
            } else if (hasEnd && !hasStart) {
                last = n;
            } else if (!hasStart && rangeContainsNode(range, n)) {
                contained.push(n);
            }
        }
        if (contained.some(function (c) { return c.nodeType === 10; })) {
            throw domException("A DocumentType node cannot be extracted", "HierarchyRequestError");
        }
        var cut = extract ? cutPoint(sc, so, ec) : null;
        var sub;
        if (first !== null) {
            if (isCharacterData(first)) {
                frag.appendChild(cloneData(sc, so, nodeLength(sc) - so));
                if (extract) {
                    sc.replaceData(so, nodeLength(sc) - so, "");
                }
            } else {
                sub = new Range();
                sub.setStart(sc, so);
                sub.setEnd(first, nodeLength(first));
                frag.appendChild(first.cloneNode(false)).appendChild(rangeContents(sub, extract));
            }
        }
        contained.forEach(function (c) {
            frag.appendChild(extract ? c : c.cloneNode(true));
        });
        if (last !== null) {
            if (isCharacterData(last)) {
                frag.appendChild(cloneData(ec, 0, eo));
                if (extract) {
                    ec.replaceData(0, eo, "");
                }
            } else {
                sub = new Range();
                sub.setStart(last, 0);
                sub.setEnd(ec, eo);
                frag.appendChild(last.cloneNode(false)).appendChild(rangeContents(sub, extract));
            }
        }
        if (extract) {
            range.setStart(cut[0], cut[1]);
            range.collapse(true);
        }
        return frag;
    }

    function hierarchyError(message) {
        return domException(message, "HierarchyRequestError");
    }

    var RangeMethods = {
        setStartBefore: function (node) {
            this.setStart(parentOrThrow(node), nodeIndex(node));
        },
        setStartAfter: function (node) {
            this.setStart(parentOrThrow(node), nodeIndex(node) + 1);
        },
        setEndBefore: function (node) {
            this.setEnd(parentOrThrow(node), nodeIndex(node));
        },
        setEndAfter: function (node) {
            this.setEnd(parentOrThrow(node), nodeIndex(node) + 1);
        },
        selectNode: function (node) {
            var parent = parentOrThrow(node);
            var index = nodeIndex(node);
            this.setStart(parent, index);
            this.setEnd(parent, index + 1);
        },
        selectNodeContents: function (node) {
            if (requireNode(node).nodeType === 10) {
                throw domException("Cannot select a DocumentType", "InvalidNodeTypeError");
            }
            this.setStart(node, 0);
            this.setEnd(node, nodeLength(node));
        },
        deleteContents: function () {
            if (this.collapsed) {
                return;
            }
            var sc = this.startContainer, so = this.startOffset;
            var ec = this.endContainer, eo = this.endOffset;
            if (sc === ec && isCharacterData(sc)) {
                sc.replaceData(so, eo - so, "");
                return;
            }
            var doomed = containedNodes(this, false);
            var cut = cutPoint(sc, so, ec);
            if (isCharacterData(sc)) {
                sc.replaceData(so, nodeLength(sc) - so, "");
            }
            doomed.forEach(function (n) {
                if (n.parentNode !== null) {
                    n.parentNode.removeChild(n);
                }
            });
            if (isCharacterData(ec)) {
                ec.replaceData(0, eo, "");
            }
            this.setStart(cut[0], cut[1]);
            this.collapse(true);
        },
        extractContents: function () {
            return rangeContents(this, true);
        },
        cloneContents: function () {
            return rangeContents(this, false);
        },
        insertNode: function (node) {
            requireNode(node);
            var sc = this.startContainer, so = this.startOffset;
            var t = sc.nodeType;
            if (t === 7 || t === 8 || (t === 3 && sc.parentNode === null) || sc === node) {
                throw hierarchyError("Cannot insert a node here");
            }
            var ref = t === 3 ? sc : sc.childNodes.item(so);
            var parent = ref === null ? sc : ref.parentNode;
            var pt = parent.nodeType;
            if ((pt !== 1 && pt !== 9 && pt !== 11) || isInclusiveAncestor(node, parent)) {
                throw hierarchyError("Cannot insert a node here");
            }
            if (t === 3) {
                ref = sc.splitText(so);
            }
            if (node === ref) {
                ref = ref.nextSibling;
            }
            if (node.parentNode !== null) {
                node.parentNode.removeChild(node);
            }
            var newOffset = (ref === null ? nodeLength(parent) : nodeIndex(ref)) +
                (node.nodeType === 11 ? nodeLength(node) : 1);
            parent.insertBefore(node, ref);
            if (this.collapsed) {
                this.setEnd(parent, newOffset);
            }
        },
        surroundContents: function (newParent) {
            requireNode(newParent);
            var sc = this.startContainer, ec = this.endContainer;
            [sc, ec].forEach(function (from) {
                for (var n = from; n !== null; n = n.parentNode) {
                    if (n.nodeType !== 3 && isInclusiveAncestor(n, sc) !== isInclusiveAncestor(n, ec)) {
                        throw domException("The range partially contains a non-Text node", "InvalidStateError");
                    }
                }
            });
            var t = newParent.nodeType;
            if (t === 9 || t === 10 || t === 11) {
                throw domException("Invalid parent node type", "InvalidNodeTypeError");
            }
            var frag = this.extractContents();
            while (newParent.firstChild !== null) {
                newParent.removeChild(newParent.firstChild);
            }
            this.insertNode(newParent);
            newParent.appendChild(frag);
            this.selectNode(newParent);
        },
        intersectsNode: function (node) {
            requireNode(node);
            if (rootOf(node) !== rootOf(this.startContainer)) {
                return false;
            }
            var parent = node.parentNode;
            if (parent === null) {
                return true;
            }
            var index = nodeIndex(node);
            return comparePoints(parent, index, this.endContainer, this.endOffset) < 0 &&
                comparePoints(parent, index + 1, this.startContainer, this.startOffset) > 0;
        },
        createContextualFragment: function (html) {
            var sc = this.startContainer;
            var el = sc.nodeType === 1 ? sc : sc.parentElement;
            var doc = sc.nodeType === 9 ? sc : sc.ownerDocument;
            if (el === null || el.localName === "html") {
                el = doc.createElement("body");
            }
            var holder = doc.createElement(el.localName);
            holder.innerHTML = String(html);
            var frag = doc.createDocumentFragment();
            while (holder.firstChild !== null) {
                frag.appendChild(holder.firstChild);
            }
            return frag;
        },
        getBoundingClientRect: function () {
            var c = this.commonAncestorContainer;
            var el = c.nodeType === 1 ? c : c.parentElement;
            return el !== null ? el.getBoundingClientRect() : new DOMRect(0, 0, 0, 0);
        },
        getClientRects: function () {
            return [this.getBoundingClientRect()];
        },
        toString: function () {
            var sc = this.startContainer, so = this.startOffset;
            var ec = this.endContainer, eo = this.endOffset;
            if (sc === ec && sc.nodeType === 3) {
                return sc.substringData(so, eo - so);
            }
            var s = sc.nodeType === 3 ? sc.substringData(so, nodeLength(sc) - so) : "";
            containedNodes(this, true).forEach(function (n) {
                if (n.nodeType === 3) {
                    s += n.data;
                }
            });
            if (ec.nodeType === 3) {
                s += ec.substringData(0, eo);
            }
            return s;
        }
    };
    Object.keys(RangeMethods).forEach(function (name) {
        define(Range.prototype, name, RangeMethods[name]);
    });

    var selections = new WeakMap();

    function Selection() {
        throw new TypeError("Illegal constructor");
    }

    function selectionRange(node, offset) {
        var r = new Range();
        r.setStart(node, offset);
        return r;
    }

    function setSelection(sel, range, backward) {
        sel._range = range;
        sel._backward = backward;
    }

    Selection.prototype = {
        constructor: Selection,
        get anchorNode() {
            return this._range === null ? null : this._backward ? this._range.endContainer : this._range.startContainer;
        },
        get anchorOffset() {
            return this._range === null ? 0 : this._backward ? this._range.endOffset : this._range.startOffset;
        },
        get focusNode() {
            return this._range === null ? null : this._backward ? this._range.startContainer : this._range.endContainer;
        },
        get focusOffset() {
            return this._range === null ? 0 : this._backward ? this._range.startOffset : this._range.endOffset;
        },
        get isCollapsed() {
            return this._range === null || this._range.collapsed;
        },
        get rangeCount() {
            return this._range === null ? 0 : 1;
        },
        get type() {
            return this._range === null ? "None" : this._range.collapsed ? "Caret" : "Range";
        },
        get direction() {
            return this._range === null || this._range.collapsed ? "none" : this._backward ? "backward" : "forward";
        },
        getRangeAt: function (index) {
            if (index !== 0 || this._range === null) {
                throw domException("Index out of range", "IndexSizeError");
            }
            return this._range;
        },
        addRange: function (range) {
            if (!(range instanceof Range)) {
                throw new TypeError("parameter 1 is not of type 'Range'");
            }
            if (this._range === null) {
                setSelection(this, range, false);
            }
        },
        removeRange: function (range) {
            if (range !== this._range || range === null) {
                throw domException("The range is not selected", "NotFoundError");
            }
            setSelection(this, null, false);
        },
        removeAllRanges: function () {
            setSelection(this, null, false);
        },
        empty: function () {
            setSelection(this, null, false);
        },
        collapse: function (node, offset) {
            if (node === null || node === undefined) {
                setSelection(this, null, false);
                return;
            }
            setSelection(this, selectionRange(node, offset === undefined ? 0 : offset >>> 0), false);
        },
        setPosition: function (node, offset) {
            this.collapse(node, offset);
        },
        collapseToStart: function () {
            if (this._range === null) {
                throw domException("No selection", "InvalidStateError");
            }
            setSelection(this, selectionRange(this._range.startContainer, this._range.startOffset), false);
        },
        collapseToEnd: function () {
            if (this._range === null) {
                throw domException("No selection", "InvalidStateError");
            }
            setSelection(this, selectionRange(this._range.endContainer, this._range.endOffset), false);
        },
        extend: function (node, offset) {
            if (this._range === null) {
                throw domException("No selection", "InvalidStateError");
            }
            this.setBaseAndExtent(this.anchorNode, this.anchorOffset, node, offset === undefined ? 0 : offset >>> 0);
        },
        setBaseAndExtent: function (anchorNode, anchorOffset, focusNode, focusOffset) {
            var r = new Range();
            anchorOffset >>>= 0;
            focusOffset >>>= 0;
            if (rootOf(requireNode(anchorNode)) === rootOf(requireNode(focusNode)) &&
                comparePoints(focusNode, focusOffset, anchorNode, anchorOffset) < 0) {
                r.setStart(focusNode, focusOffset);
                r.setEnd(anchorNode, anchorOffset);
                setSelection(this, r, true);
            } else {
                r.setStart(anchorNode, anchorOffset);
                r.setEnd(focusNode, focusOffset);
                setSelection(this, r, false);
            }
        },
        selectAllChildren: function (node) {
            var r = new Range();
            r.selectNodeContents(node);
            setSelection(this, r, false);
        },
        deleteFromDocument: function () {
            if (this._range !== null) {
                this._range.deleteContents();
            }
        },
        containsNode: function (node, allowPartial) {
            var r = this._range;
            requireNode(node);
            if (r === null || rootOf(node) !== rootOf(r.startContainer)) {
                return false;
            }
            if (allowPartial) {
                return r.intersectsNode(node);
            }
            var parent = node.parentNode;
            if (parent === null) {
                return false;
            }
            var index = nodeIndex(node);
            return comparePoints(parent, index, r.startContainer, r.startOffset) >= 0 &&
                comparePoints(parent, index + 1, r.endContainer, r.endOffset) <= 0;
        },
        modify: function () {
        },
        toString: function () {
            return this._range === null ? "" : this._range.toString();
        }
    };
    define(global, "Selection", Selection);

    function selectionOf(doc) {
        var sel = selections.get(doc);
        if (sel === undefined) {
            sel = Object.create(Selection.prototype);
            define(sel, "_range", null);
            define(sel, "_backward", false);
            selections.set(doc, sel);
        }
        return sel;
    }

    define(Document.prototype, "getSelection", function () {
        return selectionOf(this);
    });
    define(global, "getSelection", function () {
        return selectionOf(global.document);
    });

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

    function candidatesFor(root, list) {
        if (list.length !== 1) {
            return null;
        }
        var sel = list[0];
        var comp = sel.compounds[sel.compounds.length - 1];
        if (comp.anchor || comp.ids.length > 1) {
            return null;
        }
        return native.candidates(root, comp.tag, comp.ids.length === 1 ? comp.ids[0] : null, comp.classes);
    }

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
        var cands = candidatesFor(this, list);
        if (cands !== null) {
            for (var i = 0; i < cands.length; i++) {
                if (matchesList(cands[i], list, ctx)) {
                    return cands[i];
                }
            }
            return null;
        }
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
        var cands = candidatesFor(this, list);
        if (cands !== null) {
            for (var i = 0; i < cands.length; i++) {
                if (matchesList(cands[i], list, ctx)) {
                    out.push(cands[i]);
                }
            }
            return new StaticNodeList(out);
        }
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
    var reactionQueue = [];

    function queueReaction(fn) {
        reactionQueue.push(fn);
        native.queueReactions();
    }

    function flushReactions() {
        while (reactionQueue.length > 0) {
            var queue = reactionQueue;
            reactionQueue = [];
            queue.forEach(function (fn) {
                try {
                    fn();
                } catch (e) {
                    reportError(e);
                }
            });
        }
    }

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
        for (var n = node; n !== null; n = n.parentNode || (n.nodeType === 11 ? native.shadowHost(n) : null)) {
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
            if (target !== null && (target.nodeType === 1 || target.nodeType === 11)) {
                queueReaction(function () {
                    if (isConnected(target)) {
                        upgradeTree(target, true);
                    }
                });
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
            var args = [
                name,
                change === 2 ? null : e.prevValue,
                change === 3 ? null : e.newValue,
                null
            ];
            queueReaction(function () {
                callback(el, "attributeChangedCallback", args);
            });
        });
    }

    function HTMLElementConstructor() {
        var target = new.target;
        if (target === undefined) {
            if (this === null || typeof this !== "object" || !(this instanceof HTMLElementConstructor)) {
                throw new TypeError("Failed to construct 'HTMLElement': Please use the 'new' operator");
            }
            target = this.constructor;
        }
        var def = definitionsByCtor.get(target);
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
        Object.setPrototypeOf(el, new.target === undefined ? Object.getPrototypeOf(this) : new.target.prototype);
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
        if (body instanceof FormData) {
            return native.utf8Decode(encodeMultipart(body).bytes);
        }
        if (body instanceof Blob) {
            return native.utf8Decode(body._bytes);
        }
        return String(body);
    }

    function defaultContentType(body) {
        if (typeof body === "string" || body instanceof String) {
            return "text/plain;charset=UTF-8";
        }
        if (typeof URLSearchParams === "function" && body instanceof URLSearchParams) {
            return "application/x-www-form-urlencoded;charset=UTF-8";
        }
        if (body instanceof Blob) {
            return body.type || null;
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
            var type = this._headers && this._headers.get ? this._headers.get("content-type") : null;
            return this.arrayBuffer().then(function (buffer) {
                return new Blob([buffer], { type: type || "" });
            });
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
        var multipart = body instanceof FormData ? encodeMultipart(body) : null;
        var bodyText = multipart ? native.utf8Decode(multipart.bytes) : bodyToString(body);
        var contentType = multipart ? multipart.type : defaultContentType(body);
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
                if (scheme === "blob") {
                    var blob = blobRegistry.get(url);
                    if (blob === undefined || method !== "GET") {
                        fail(new TypeError("Failed to fetch"));
                        return;
                    }
                    var blobResponse = new Response(blob._bytes, { headers: blob._type ? { "content-type": blob._type } : {} });
                    blobResponse._url = url;
                    finished = true;
                    if (signal) {
                        signal.removeEventListener("abort", onAbort);
                    }
                    resolve(blobResponse);
                    return;
                }
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

    function defineGetter(proto, name, get, set) {
        Object.defineProperty(proto, name, { get: get, set: set, configurable: true, enumerable: true });
    }

    function defineMissing(target, name, value) {
        if (!(name in target)) {
            define(target, name, value);
        }
    }

    function Storage() {
        throw new TypeError("Illegal constructor");
    }

    var storageQuota = 5 * 1024 * 1024;

    function storageKey() {
        var origin = native.origin(location.href);
        if (origin !== "null") {
            return origin;
        }
        return location.protocol === "file:" ? "file://" : null;
    }

    function parseStore(text, data) {
        data.clear();
        if (!text) {
            return;
        }
        try {
            var obj = JSON.parse(text);
            Object.keys(obj).forEach(function (k) {
                data.set(k, String(obj[k]));
            });
        } catch (e) {
        }
    }

    function makeStorage(persistent) {
        var key = storageKey();
        var data = new Map();
        var read = persistent ? native.storageRead : native.sessionRead;
        var write = persistent ? native.storageWrite : native.sessionWrite;
        var store = Object.create(Storage.prototype);
        function load() {
            if (key !== null) {
                parseStore(read(key), data);
            }
        }
        function save(next) {
            var obj = {};
            next.forEach(function (v, k) {
                obj[k] = v;
            });
            var text = JSON.stringify(obj);
            if (text.length * 2 > storageQuota) {
                throw domException("The quota has been exceeded.", "QuotaExceededError");
            }
            if (key !== null) {
                write(key, next.size ? text : null);
            }
            data = next;
        }
        function change(fn) {
            load();
            var next = new Map(data);
            fn(next);
            save(next);
        }
        load();
        define(store, "_get", function (k) {
            return data.has(k) ? data.get(k) : null;
        });
        define(store, "_keys", function () {
            return Array.from(data.keys());
        });
        define(store, "_change", change);
        return new Proxy(store, {
            get: function (target, prop) {
                if (typeof prop === "symbol" || prop in target) {
                    return target[prop];
                }
                var v = target._get(prop);
                return v === null ? undefined : v;
            },
            set: function (target, prop, value) {
                if (typeof prop === "symbol") {
                    target[prop] = value;
                } else {
                    target.setItem(prop, value);
                }
                return true;
            },
            has: function (target, prop) {
                return prop in target || (typeof prop === "string" && target._get(prop) !== null);
            },
            deleteProperty: function (target, prop) {
                if (typeof prop === "string" && !(prop in target)) {
                    target.removeItem(prop);
                }
                return true;
            },
            ownKeys: function (target) {
                return target._keys();
            },
            getOwnPropertyDescriptor: function (target, prop) {
                if (typeof prop !== "string" || prop in target) {
                    return undefined;
                }
                var v = target._get(prop);
                return v === null ? undefined : { value: v, writable: true, enumerable: true, configurable: true };
            }
        });
    }

    define(Storage.prototype, "getItem", function (k) {
        return this._get(String(k));
    });
    define(Storage.prototype, "setItem", function (k, v) {
        k = String(k);
        v = String(v);
        this._change(function (m) {
            m.set(k, v);
        });
    });
    define(Storage.prototype, "removeItem", function (k) {
        k = String(k);
        this._change(function (m) {
            m.delete(k);
        });
    });
    define(Storage.prototype, "clear", function () {
        this._change(function (m) {
            m.clear();
        });
    });
    define(Storage.prototype, "key", function (i) {
        var keys = this._keys();
        i = Number(i) >>> 0;
        return i < keys.length ? keys[i] : null;
    });
    Object.defineProperty(Storage.prototype, "length", { get: function () { return this._keys().length; }, configurable: true });
    define(Storage.prototype, Symbol.toStringTag, "Storage");
    define(global, "Storage", Storage);

    var localStore = null;
    var sessionStore = null;
    defineGetter(Window.prototype, "localStorage", function () {
        return localStore || (localStore = makeStorage(true));
    });
    defineGetter(Window.prototype, "sessionStorage", function () {
        return sessionStore || (sessionStore = makeStorage(false));
    });

    defineGetter(Document.prototype, "readyState", function () {
        return ["loading", "interactive", "complete"][native.readyState()] || "loading";
    });
    defineGetter(Document.prototype, "hidden", function () {
        return false;
    });
    defineGetter(Document.prototype, "visibilityState", function () {
        return "visible";
    });
    defineMissing(Document.prototype, "hasFocus", function () {
        return true;
    });

    defineMissing(Element.prototype, "getAttributeNames", function () {
        var attrs = this.attributes, names = [];
        for (var i = 0; i < attrs.length; i++) {
            names.push(attrs.item(i).name);
        }
        return names;
    });
    defineMissing(Element.prototype, "toggleAttribute", function (name, force) {
        var has = this.hasAttribute(name);
        if (force === undefined ? has : !force) {
            if (has) {
                this.removeAttribute(name);
            }
            return false;
        }
        if (!has) {
            this.setAttribute(name, "");
        }
        return true;
    });
    var voidElements = /^(area|base|basefont|bgsound|br|col|embed|frame|hr|img|input|keygen|link|meta|param|source|track|wbr)$/;
    var rawTextElements = /^(style|script|xmp|iframe|noembed|noframes|plaintext|noscript)$/;
    var htmlNamespace = "http://www.w3.org/1999/xhtml";

    function escapeHtml(text, attribute) {
        text = text.replace(/&/g, "&amp;").replace(/\u00a0/g, "&nbsp;");
        return attribute ? text.replace(/"/g, "&quot;") : text.replace(/</g, "&lt;").replace(/>/g, "&gt;");
    }

    function serializeHtmlChildren(node) {
        var out = "";
        var parent = node.nodeType === 1 && node.localName === "template" && node.content ? node.content : node;
        for (var c = parent.firstChild; c !== null; c = c.nextSibling) {
            out += serializeHtmlNode(c, node);
        }
        return out;
    }

    function serializeHtmlNode(node, parent) {
        switch (node.nodeType) {
        case 1:
            var html = node.namespaceURI === htmlNamespace || node.namespaceURI === null;
            var name = html ? node.localName : node.tagName;
            var s = "<" + name;
            for (var i = 0; i < node.attributes.length; i++) {
                var a = node.attributes[i];
                s += " " + a.name + "=\"" + escapeHtml(a.value, true) + "\"";
            }
            s += ">";
            if (html && voidElements.test(name)) {
                return s;
            }
            return s + serializeHtmlChildren(node) + "</" + name + ">";
        case 3:
        case 4:
            if (parent && parent.nodeType === 1 && rawTextElements.test(parent.localName) &&
                (parent.namespaceURI === htmlNamespace || parent.namespaceURI === null)) {
                return node.data;
            }
            return escapeHtml(node.data, false);
        case 7:
            return "<?" + node.target + " " + node.data + ">";
        case 8:
            return "<!--" + node.data + "-->";
        case 10:
            return "<!DOCTYPE " + node.name + ">";
        case 9:
        case 11:
            return serializeHtmlChildren(node);
        }
        return "";
    }

    var templateContents = new WeakMap();
    var inertDocument = null;

    function isTemplate(node) {
        return node.nodeType === 1 && node.localName === "template" &&
            (node.namespaceURI === htmlNamespace || node.namespaceURI === null);
    }

    function templateContent(el) {
        var frag = templateContents.get(el);
        if (frag === undefined) {
            if (inertDocument === null) {
                inertDocument = document.implementation.createHTMLDocument("");
            }
            frag = inertDocument.createDocumentFragment();
            templateContents.set(el, frag);
            while (el.firstChild) {
                frag.appendChild(el.firstChild);
            }
        }
        return frag;
    }

    if (typeof HTMLTemplateElement === "function") {
        defineGetter(HTMLTemplateElement.prototype, "content", function () {
            return templateContent(this);
        });
    }
    var pendingTemplates = [];

    function moveTemplateChildren(el) {
        var frag = templateContent(el);
        while (el.firstChild) {
            frag.appendChild(el.firstChild);
        }
    }

    function noteTemplate(el) {
        if (isTemplate(el) && el.ownerDocument === document) {
            moveTemplateChildren(el);
            if (document.readyState === "loading") {
                pendingTemplates.push(el);
            }
        }
        return pendingTemplates.length > 0;
    }

    function moveTemplates() {
        pendingTemplates.forEach(moveTemplateChildren);
        if (document.readyState !== "loading") {
            pendingTemplates = [];
        }
        return pendingTemplates.length > 0;
    }

    document.addEventListener("DOMContentLoaded", function () {
        moveTemplates();
        Array.prototype.slice.call(document.getElementsByTagName("template")).forEach(function (el) {
            if (isTemplate(el)) {
                templateContent(el);
            }
        });
    });

    var innerHTMLDescriptor = Object.getOwnPropertyDescriptor(Element.prototype, "innerHTML");
    var nativeInnerHTMLSet = innerHTMLDescriptor ? innerHTMLDescriptor.set : undefined;
    defineGetter(Element.prototype, "innerHTML", function () {
        return serializeHtmlChildren(this);
    }, nativeInnerHTMLSet === undefined ? undefined : function (html) {
        if (this.nodeType === 1 && (this.namespaceURI === htmlNamespace || this.namespaceURI === null) &&
            /^(script|style|xmp|iframe|noembed|noframes|plaintext)$/.test(this.localName)) {
            this.textContent = html === null ? "" : String(html);
            return;
        }
        if (!isTemplate(this)) {
            nativeInnerHTMLSet.call(this, html);
            return;
        }
        var frag = templateContent(this);
        while (frag.firstChild) {
            frag.removeChild(frag.firstChild);
        }
        nativeInnerHTMLSet.call(this, html);
        while (this.firstChild) {
            frag.appendChild(this.firstChild);
        }
    });

    var nativeCloneNode = Node.prototype.cloneNode;
    define(Node.prototype, "cloneNode", function (deep) {
        var copy = nativeCloneNode.apply(this, arguments);
        if (deep && isTemplate(this) && templateContents.has(this)) {
            var target = templateContent(copy);
            for (var c = templateContents.get(this).firstChild; c !== null; c = c.nextSibling) {
                target.appendChild(c.cloneNode(true));
            }
        }
        return copy;
    });
    defineGetter(Element.prototype, "outerHTML", function () {
        return serializeHtmlNode(this, this.parentNode);
    }, function (html) {
        var parent = this.parentNode;
        if (parent === null) {
            return;
        }
        if (parent.nodeType === 9) {
            throw domException("Cannot set outerHTML on the document element", "NoModificationAllowedError");
        }
        var holder = this.ownerDocument.createElement(parent.nodeType === 1 ? parent.localName : "body");
        holder.innerHTML = String(html);
        while (holder.firstChild) {
            parent.insertBefore(holder.firstChild, this);
        }
        parent.removeChild(this);
    });
    defineMissing(Element.prototype, "getBoundingClientRect", function () {
        return new DOMRect(0, 0, 0, 0);
    });
    defineMissing(Element.prototype, "getClientRects", function () {
        return [];
    });
    defineMissing(Element.prototype, "scrollIntoView", function () {
    });
    function elementScroll(el, args, relative) {
        var left, top;
        if (args.length === 1 && args[0] !== null && typeof args[0] === "object") {
            left = args[0].left;
            top = args[0].top;
        } else {
            left = args[0];
            top = args[1];
        }
        if (left !== undefined && isFinite(left)) {
            el.scrollLeft = (relative ? el.scrollLeft : 0) + Number(left);
        }
        if (top !== undefined && isFinite(top)) {
            el.scrollTop = (relative ? el.scrollTop : 0) + Number(top);
        }
    }
    defineMissing(Element.prototype, "scroll", function () {
        elementScroll(this, arguments, false);
    });
    defineMissing(Element.prototype, "scrollTo", function () {
        elementScroll(this, arguments, false);
    });
    defineMissing(Element.prototype, "scrollBy", function () {
        elementScroll(this, arguments, true);
    });
    defineGetter(Node.prototype, "isConnected", function () {
        return isConnected(this);
    });
    define(Node.prototype, "getRootNode", function (options) {
        var composed = options !== undefined && options !== null && !!options.composed;
        var n = this;
        for (;;) {
            while (n.parentNode) {
                n = n.parentNode;
            }
            var host = n.nodeType === 11 ? native.shadowHost(n) : null;
            if (host === null) {
                return n;
            }
            if (!composed) {
                return asShadowRoot(n);
            }
            n = host;
        }
    });

    function convertNodes(doc, nodes) {
        var list = Array.prototype.map.call(nodes, function (n) {
            return n instanceof Node ? n : doc.createTextNode(String(n));
        });
        if (list.length === 1) {
            return list[0];
        }
        var frag = doc.createDocumentFragment();
        list.forEach(function (n) {
            frag.appendChild(n);
        });
        return frag;
    }

    function docOf(node) {
        return node.nodeType === 9 ? node : node.ownerDocument;
    }

    function viableSibling(node, nodes, forward) {
        var set = Array.prototype.slice.call(nodes);
        var n = forward ? node.nextSibling : node.previousSibling;
        while (n !== null && set.indexOf(n) >= 0) {
            n = forward ? n.nextSibling : n.previousSibling;
        }
        return n;
    }

    var ChildNodeMethods = {
        before: function () {
            var parent = this.parentNode;
            if (parent === null) {
                return;
            }
            var prev = viableSibling(this, arguments, false);
            var node = convertNodes(docOf(this), arguments);
            parent.insertBefore(node, prev === null ? parent.firstChild : prev.nextSibling);
        },
        after: function () {
            var parent = this.parentNode;
            if (parent === null) {
                return;
            }
            var next = viableSibling(this, arguments, true);
            parent.insertBefore(convertNodes(docOf(this), arguments), next);
        },
        replaceWith: function () {
            var parent = this.parentNode;
            if (parent === null) {
                return;
            }
            var next = viableSibling(this, arguments, true);
            var node = convertNodes(docOf(this), arguments);
            if (this.parentNode === parent) {
                parent.replaceChild(node, this);
            } else {
                parent.insertBefore(node, next);
            }
        },
        remove: function () {
            if (this.parentNode !== null) {
                this.parentNode.removeChild(this);
            }
        }
    };
    [Element, CharacterData, global.DocumentType].forEach(function (iface) {
        if (typeof iface !== "function") {
            return;
        }
        Object.keys(ChildNodeMethods).forEach(function (name) {
            define(iface.prototype, name, ChildNodeMethods[name]);
        });
    });

    var ParentNodeMethods = {
        append: function () {
            this.appendChild(convertNodes(docOf(this), arguments));
        },
        prepend: function () {
            this.insertBefore(convertNodes(docOf(this), arguments), this.firstChild);
        },
        replaceChildren: function () {
            var node = convertNodes(docOf(this), arguments);
            while (this.firstChild !== null) {
                this.removeChild(this.firstChild);
            }
            this.appendChild(node);
        }
    };
    [Element, Document, DocumentFragment].forEach(function (iface) {
        Object.keys(ParentNodeMethods).forEach(function (name) {
            define(iface.prototype, name, ParentNodeMethods[name]);
        });
    });

    function ShadowRoot() {
        throw new TypeError("Illegal constructor");
    }
    Object.setPrototypeOf(ShadowRoot.prototype, DocumentFragment.prototype);
    define(ShadowRoot.prototype, Symbol.toStringTag, "ShadowRoot");
    defineMissing(global, "ShadowRoot", ShadowRoot);

    var shadowModes = new WeakMap();
    var shadowHostNames = /^(article|aside|blockquote|body|div|footer|h[1-6]|header|main|nav|p|section|span)$/;

    function asShadowRoot(frag) {
        if (frag !== null && Object.getPrototypeOf(frag) !== ShadowRoot.prototype) {
            Object.setPrototypeOf(frag, ShadowRoot.prototype);
        }
        return frag;
    }

    function shadowState(root) {
        var state = shadowModes.get(root);
        if (state === undefined) {
            state = { mode: "open", declarative: true, delegatesFocus: false };
            shadowModes.set(root, state);
        }
        return state;
    }

    define(Element.prototype, "attachShadow", function (init) {
        var mode = init === undefined || init === null ? undefined : init.mode;
        if (mode !== "open" && mode !== "closed") {
            throw new TypeError("Failed to execute 'attachShadow': mode must be 'open' or 'closed'");
        }
        if (this.namespaceURI !== htmlNamespace ||
            (!shadowHostNames.test(this.localName) && this.localName.indexOf("-") < 0)) {
            throw new DOMException("This element does not support attachShadow", "NotSupportedError");
        }
        var existing = native.shadowRoot(this);
        if (existing !== null) {
            var state = shadowState(existing);
            if (!state.declarative || state.mode !== mode) {
                throw new DOMException("Shadow root cannot be created on a host which already hosts a shadow tree.", "NotSupportedError");
            }
            while (existing.firstChild) {
                existing.removeChild(existing.firstChild);
            }
            state.declarative = false;
            return asShadowRoot(existing);
        }
        var root = native.shadowAttach(this);
        if (root === null) {
            throw new DOMException("Shadow root cannot be created", "NotSupportedError");
        }
        shadowModes.set(root, { mode: mode, declarative: false, delegatesFocus: !!init.delegatesFocus });
        return asShadowRoot(root);
    });
    defineGetter(Element.prototype, "shadowRoot", function () {
        var root = native.shadowRoot(this);
        if (root === null || shadowState(root).mode !== "open") {
            return null;
        }
        return asShadowRoot(root);
    });
    defineGetter(ShadowRoot.prototype, "host", function () {
        return native.shadowHost(this);
    });
    defineGetter(ShadowRoot.prototype, "mode", function () {
        return shadowState(this).mode;
    });
    defineGetter(ShadowRoot.prototype, "delegatesFocus", function () {
        return shadowState(this).delegatesFocus;
    });
    defineGetter(ShadowRoot.prototype, "slotAssignment", function () {
        return "named";
    });
    defineGetter(ShadowRoot.prototype, "clonable", function () {
        return false;
    });
    defineGetter(ShadowRoot.prototype, "activeElement", function () {
        var el = document.activeElement;
        while (el !== null) {
            var root = el.getRootNode();
            if (root === this) {
                return el;
            }
            el = root.nodeType === 11 ? native.shadowHost(root) : null;
        }
        return null;
    });
    defineGetter(ShadowRoot.prototype, "innerHTML", function () {
        return serializeHtmlChildren(this);
    }, function (html) {
        var t = document.createElement("template");
        t.innerHTML = html === null ? "" : String(html);
        while (this.firstChild) {
            this.removeChild(this.firstChild);
        }
        this.appendChild(t.content);
    });
    define(ShadowRoot.prototype, "setHTMLUnsafe", function (html) {
        this.innerHTML = html;
    });

    function slotName(node) {
        if (node.nodeType === 1) {
            return node.getAttribute("slot") || "";
        }
        return node.nodeType === 3 ? "" : null;
    }

    function findSlot(root, name) {
        for (var n = root.firstChild; n !== null; n = followingIn(n, root, false)) {
            if (n.nodeType === 1 && n.localName === "slot" && n.namespaceURI === htmlNamespace &&
                (n.getAttribute("name") || "") === name) {
                return n;
            }
        }
        return null;
    }

    function slotAssigned(slot) {
        var root = slot.getRootNode();
        var host = root.nodeType === 11 ? native.shadowHost(root) : null;
        var list = [];
        if (host === null) {
            return list;
        }
        var name = slot.getAttribute("name") || "";
        if (findSlot(root, name) !== slot) {
            return list;
        }
        for (var c = host.firstChild; c !== null; c = c.nextSibling) {
            if (slotName(c) === name) {
                list.push(c);
            }
        }
        return list;
    }

    function slotFlattened(slot, out) {
        var assigned = slotAssigned(slot);
        if (assigned.length === 0) {
            assigned = Array.prototype.slice.call(slot.childNodes).filter(function (n) {
                return n.nodeType === 1 || n.nodeType === 3;
            });
        }
        assigned.forEach(function (n) {
            if (n.nodeType === 1 && n.localName === "slot" && n.namespaceURI === htmlNamespace &&
                n.getRootNode().nodeType === 11 && native.shadowHost(n.getRootNode()) !== null) {
                slotFlattened(n, out);
            } else {
                out.push(n);
            }
        });
        return out;
    }

    function HTMLSlotElement() {
        throw new TypeError("Illegal constructor");
    }
    HTMLSlotElement.prototype = Object.create(HTMLElement.prototype, {
        constructor: { value: HTMLSlotElement, writable: true, configurable: true }
    });
    define(HTMLSlotElement.prototype, Symbol.toStringTag, "HTMLSlotElement");
    defineGetter(HTMLSlotElement.prototype, "name", function () {
        return this.getAttribute("name") || "";
    }, function (value) {
        this.setAttribute("name", value);
    });
    define(HTMLSlotElement.prototype, "assignedNodes", function (options) {
        if (options && options.flatten) {
            return slotFlattened(this, []);
        }
        return slotAssigned(this);
    });
    define(HTMLSlotElement.prototype, "assignedElements", function (options) {
        return this.assignedNodes(options).filter(function (n) {
            return n.nodeType === 1;
        });
    });
    define(HTMLSlotElement.prototype, "assign", function () {
    });
    defineMissing(global, "HTMLSlotElement", HTMLSlotElement);

    defineGetter(Element.prototype, "slot", function () {
        return this.getAttribute("slot") || "";
    }, function (value) {
        this.setAttribute("slot", value);
    });
    [Element, Text].forEach(function (iface) {
        defineGetter(iface.prototype, "assignedSlot", function () {
            var host = this.parentNode;
            var root = host !== null && host.nodeType === 1 ? native.shadowRoot(host) : null;
            if (root === null || shadowState(root).mode !== "open") {
                return null;
            }
            var name = slotName(this);
            return name === null ? null : findSlot(root, name);
        });
    });
    define(DocumentFragment.prototype, "getElementById", function (id) {
        id = String(id);
        for (var n = this.firstChild; n !== null; n = followingIn(n, this, false)) {
            if (n.nodeType === 1 && n.id === id) {
                return n;
            }
        }
        return null;
    });

    function adjacentTarget(el, where, node) {
        var parent = el.parentNode;
        switch (String(where).toLowerCase()) {
        case "beforebegin":
            if (parent === null) {
                return null;
            }
            parent.insertBefore(node, el);
            return node;
        case "afterbegin":
            el.insertBefore(node, el.firstChild);
            return node;
        case "beforeend":
            el.appendChild(node);
            return node;
        case "afterend":
            if (parent === null) {
                return null;
            }
            parent.insertBefore(node, el.nextSibling);
            return node;
        }
        throw domException("The value provided ('" + where + "') is not one of 'beforeBegin', 'afterBegin', 'beforeEnd', or 'afterEnd'.", "SyntaxError");
    }

    define(Element.prototype, "insertAdjacentElement", function (where, el) {
        if (!(el instanceof Element)) {
            throw new TypeError("parameter 2 is not of type 'Element'");
        }
        return adjacentTarget(this, where, el);
    });
    define(Element.prototype, "insertAdjacentText", function (where, text) {
        adjacentTarget(this, where, this.ownerDocument.createTextNode(String(text)));
    });
    define(Element.prototype, "insertAdjacentHTML", function (where, html) {
        var pos = String(where).toLowerCase();
        var context = pos === "beforebegin" || pos === "afterend" ? this.parentElement : this;
        if ((pos === "beforebegin" || pos === "afterend") && (this.parentNode === null || this.parentNode.nodeType === 9)) {
            throw domException("The element has no parent", "NoModificationAllowedError");
        }
        var doc = this.ownerDocument;
        var holder = doc.createElement(context === null || context.localName === "html" ? "body" : context.localName);
        holder.innerHTML = String(html);
        var frag = doc.createDocumentFragment();
        while (holder.firstChild !== null) {
            frag.appendChild(holder.firstChild);
        }
        adjacentTarget(this, pos, frag);
    });

    var focused = new WeakMap();

    function focusEvent(type, related, bubbles) {
        var init = { bubbles: bubbles, cancelable: false, relatedTarget: related };
        return typeof FocusEvent === "function" ? new FocusEvent(type, init) : new Event(type, init);
    }

    function setFocus(doc, el) {
        var old = focused.get(doc) || null;
        if (old === el) {
            return;
        }
        focused.set(doc, el);
        if (old !== null && old.isConnected) {
            old.dispatchEvent(focusEvent("blur", el, false));
            old.dispatchEvent(focusEvent("focusout", el, true));
        }
        if (el !== null) {
            el.dispatchEvent(focusEvent("focus", old, false));
            el.dispatchEvent(focusEvent("focusin", old, true));
        }
    }

    define(HTMLElement.prototype, "focus", function () {
        if (this.isConnected && !this.disabled) {
            setFocus(this.ownerDocument, this);
        }
    });
    define(HTMLElement.prototype, "blur", function () {
        if (focused.get(this.ownerDocument) === this) {
            setFocus(this.ownerDocument, null);
        }
    });
    defineGetter(Document.prototype, "activeElement", function () {
        var el = focused.get(this) || null;
        return el !== null && el.isConnected && el.ownerDocument === this ? el : this.body || this.documentElement;
    });
    defineGetter(Document.prototype, "origin", function () {
        return this.location ? this.location.origin : "null";
    });

    var documentNoops = {
        open: function () {
            return this;
        },
        close: function () {
        },
        clear: function () {
        },
        captureEvents: function () {
        },
        releaseEvents: function () {
        },
        execCommand: function () {
            return false;
        },
        queryCommandEnabled: function () {
            return false;
        },
        queryCommandIndeterm: function () {
            return false;
        },
        queryCommandState: function () {
            return false;
        },
        queryCommandSupported: function () {
            return false;
        },
        queryCommandValue: function () {
            return "";
        },
        getElementsByName: function (name) {
            name = String(name);
            var out = [];
            var all = this.getElementsByTagName("*");
            for (var i = 0; i < all.length; i++) {
                if (all[i].getAttribute("name") === name) {
                    out.push(all[i]);
                }
            }
            return new StaticNodeList(out);
        }
    };
    Object.keys(documentNoops).forEach(function (name) {
        defineMissing(Document.prototype, name, documentNoops[name]);
    });

    var windowCompat = {
        focus: function () {
        },
        blur: function () {
        },
        stop: function () {
        },
        print: function () {
        },
        close: function () {
        },
        captureEvents: function () {
        },
        releaseEvents: function () {
        },
        open: function () {
            return null;
        },
        alert: function () {
        },
        confirm: function () {
            return false;
        },
        prompt: function () {
            return null;
        },
        postMessage: function (message, targetOrigin) {
            var data = structuredClone(message);
            var origin = native.origin(global.location.href);
            var target = typeof targetOrigin === "object" && targetOrigin !== null ? targetOrigin.targetOrigin : targetOrigin;
            if (target !== undefined && target !== "*" && target !== "/" && native.origin(String(target)) !== origin) {
                return;
            }
            setTimeout(function () {
                var evt = typeof MessageEvent === "function" ? new MessageEvent("message", { data: data, origin: origin, source: global }) : new Event("message");
                if (!("data" in evt) || evt.data !== data) {
                    define(evt, "data", data);
                    define(evt, "origin", origin);
                    define(evt, "source", global);
                }
                global.dispatchEvent(evt);
            }, 0);
        }
    };
    Object.keys(windowCompat).forEach(function (name) {
        if (typeof global[name] !== "function") {
            define(global, name, windowCompat[name]);
        }
    });

    function controlValue(el) {
        return el.value === undefined || el.value === null ? "" : String(el.value);
    }

    function numericValue(el, attr) {
        var v = el.getAttribute(attr);
        if (v === null || v === "") {
            return NaN;
        }
        return Number(v);
    }

    function radioGroupChecked(el) {
        if (el.name === "") {
            return el.checked;
        }
        var scope = el.form || el.ownerDocument;
        var radios = scope.getElementsByTagName("input");
        for (var i = 0; i < radios.length; i++) {
            var r = radios[i];
            if (r.type === "radio" && r.name === el.name && r.form === el.form && r.checked) {
                return true;
            }
        }
        return false;
    }

    var emailPattern = /^[a-zA-Z0-9.!#$%&'*+\/=?^_`{|}~-]+@[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?(?:\.[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*$/;

    function computeValidity(el) {
        var v = {
            valueMissing: false, typeMismatch: false, patternMismatch: false, tooLong: false,
            tooShort: false, rangeUnderflow: false, rangeOverflow: false, stepMismatch: false,
            badInput: false, customError: typeof el._customValidity === "string" && el._customValidity !== ""
        };
        var tag = el.tagName;
        var type = tag === "INPUT" ? el.type : "";
        var value = tag === "SELECT" || tag === "INPUT" || tag === "TEXTAREA" ? controlValue(el) : "";
        if (el.willValidate && el.required) {
            if (type === "checkbox") {
                v.valueMissing = !el.checked;
            } else if (type === "radio") {
                v.valueMissing = !radioGroupChecked(el);
            } else if (tag === "SELECT") {
                v.valueMissing = el.selectedIndex < 0 || value === "";
            } else if (tag !== "BUTTON") {
                v.valueMissing = value === "";
            }
        }
        if (tag === "INPUT" && value !== "") {
            if (type === "email") {
                v.typeMismatch = (el.multiple ? value.split(",") : [value]).some(function (s) {
                    return !emailPattern.test(s.trim());
                });
            } else if (type === "url") {
                try {
                    new URL(value);
                } catch (e) {
                    v.typeMismatch = true;
                }
            }
            var pattern = el.getAttribute("pattern");
            if (pattern !== null && /^(text|search|url|tel|email|password)$/.test(type)) {
                try {
                    v.patternMismatch = !new RegExp("^(?:" + pattern + ")$", "v").test(value);
                } catch (e) {
                    v.patternMismatch = false;
                }
            }
            if (type === "number" || type === "range") {
                var n = Number(value);
                if (isNaN(n)) {
                    v.badInput = true;
                } else {
                    v.rangeUnderflow = n < numericValue(el, "min");
                    v.rangeOverflow = n > numericValue(el, "max");
                    var step = numericValue(el, "step");
                    var base = numericValue(el, "min");
                    if (el.getAttribute("step") !== "any" && !isNaN(step) && step > 0) {
                        var q = (n - (isNaN(base) ? 0 : base)) / step;
                        v.stepMismatch = Math.abs(q - Math.round(q)) > 1e-9;
                    }
                }
            }
        }
        v.valid = !Object.keys(v).some(function (k) {
            return v[k];
        });
        return v;
    }

    function validationMessage(el, v) {
        if (v.customError) {
            return el._customValidity;
        }
        if (v.valueMissing) {
            return "Please fill out this field.";
        }
        if (v.typeMismatch) {
            return el.type === "email" ? "Please enter an email address." : "Please enter a URL.";
        }
        if (v.patternMismatch) {
            return "Please match the requested format.";
        }
        if (v.rangeUnderflow || v.rangeOverflow || v.stepMismatch || v.badInput) {
            return "Please enter a valid value.";
        }
        return "";
    }

    function checkControl(el) {
        if (!el.willValidate || computeValidity(el).valid) {
            return true;
        }
        el.dispatchEvent(new Event("invalid", { cancelable: true }));
        return false;
    }

    var validatedControls = [global.HTMLInputElement, global.HTMLTextAreaElement, global.HTMLSelectElement,
        global.HTMLButtonElement, global.HTMLFieldSetElement, global.HTMLOutputElement, global.HTMLObjectElement];
    validatedControls.forEach(function (iface) {
        if (typeof iface !== "function") {
            return;
        }
        accessor(iface, "validity", function () {
            var v = computeValidity(this);
            var state = Object.create(typeof ValidityState === "function" ? ValidityState.prototype : Object.prototype);
            Object.keys(v).forEach(function (k) {
                Object.defineProperty(state, k, { value: v[k], enumerable: true });
            });
            return state;
        });
        accessor(iface, "validationMessage", function () {
            return this.willValidate ? validationMessage(this, computeValidity(this)) : "";
        });
        define(iface.prototype, "setCustomValidity", function (message) {
            define(this, "_customValidity", String(message));
        });
        define(iface.prototype, "checkValidity", function () {
            return checkControl(this);
        });
        define(iface.prototype, "reportValidity", function () {
            return checkControl(this);
        });
    });
    accessor(global.HTMLFieldSetElement, "willValidate", function () {
        return false;
    });
    accessor(global.HTMLOutputElement, "willValidate", function () {
        return false;
    });
    accessor(global.HTMLObjectElement, "willValidate", function () {
        return false;
    });
    accessor(global.HTMLButtonElement, "willValidate", function () {
        return !this.disabled && this.type === "submit";
    });

    function formValid(form) {
        var ok = true;
        Array.prototype.forEach.call(form.elements, function (el) {
            if (!checkControl(el)) {
                ok = false;
            }
        });
        return ok;
    }

    if (typeof global.HTMLFormElement === "function") {
        define(global.HTMLFormElement.prototype, "checkValidity", function () {
            return formValid(this);
        });
        define(global.HTMLFormElement.prototype, "reportValidity", function () {
            return formValid(this);
        });
        define(global.HTMLFormElement.prototype, "reset", function () {
            if (!this.dispatchEvent(new Event("reset", { bubbles: true, cancelable: true }))) {
                return;
            }
            Array.prototype.forEach.call(this.elements, function (el) {
                if (el.tagName === "INPUT") {
                    if (el.type === "checkbox" || el.type === "radio") {
                        el.checked = el.defaultChecked;
                    } else if (el.type !== "file") {
                        el.value = el.defaultValue;
                    }
                } else if (el.tagName === "TEXTAREA") {
                    el.value = el.defaultValue;
                } else if (el.tagName === "SELECT") {
                    Array.prototype.forEach.call(el.options, function (o) {
                        o.selected = o.defaultSelected;
                    });
                }
            });
        });
        reflectString(global.HTMLFormElement, "name", "name");
        reflectString(global.HTMLFormElement, "autocomplete", "autocomplete");
        reflectBool(global.HTMLFormElement, "noValidate", "novalidate");
        accessor(global.HTMLFormElement, "encoding", function () {
            return this.enctype;
        }, function (v) {
            this.enctype = v;
        });
    }

    [global.HTMLInputElement, global.HTMLTextAreaElement].forEach(function (iface) {
        if (typeof iface !== "function") {
            return;
        }
        define(iface.prototype, "setSelectionRange", function (start, end, direction) {
            var len = controlValue(this).length;
            start = Math.min(start >>> 0, len);
            end = Math.min(end >>> 0, len);
            define(this, "_selStart", Math.min(start, end));
            define(this, "_selEnd", end);
            this.dispatchEvent(new Event("select", { bubbles: true }));
        });
        define(iface.prototype, "select", function () {
            this.setSelectionRange(0, controlValue(this).length);
        });
        define(iface.prototype, "setRangeText", function (replacement, start, end, mode) {
            var value = controlValue(this);
            var selStart = this.selectionStart, selEnd = this.selectionEnd;
            if (start === undefined) {
                start = selStart;
                end = selEnd;
            }
            start = Math.min(start >>> 0, value.length);
            end = Math.min(end >>> 0, value.length);
            if (start > end) {
                throw domException("The provided start value is larger than the end value", "IndexSizeError");
            }
            replacement = String(replacement);
            this.value = value.slice(0, start) + replacement + value.slice(end);
            var newEnd = start + replacement.length;
            if (mode === "select") {
                selStart = start;
                selEnd = newEnd;
            } else if (mode === "start") {
                selStart = selEnd = start;
            } else if (mode === "end") {
                selStart = selEnd = newEnd;
            } else {
                var delta = replacement.length - (end - start);
                if (selStart > end) {
                    selStart += delta;
                } else if (selStart > start) {
                    selStart = start;
                }
                if (selEnd > end) {
                    selEnd += delta;
                } else if (selEnd > start) {
                    selEnd = newEnd;
                }
            }
            define(this, "_selStart", selStart);
            define(this, "_selEnd", selEnd);
        });
    });
    reflectString(global.HTMLTextAreaElement, "wrap", "wrap");
    [["cols", 20], ["rows", 2]].forEach(function (pair) {
        accessor(global.HTMLTextAreaElement, pair[0], function () {
            var n = parseInt(this.getAttribute(pair[0]), 10);
            return n > 0 ? n : pair[1];
        }, function (v) {
            this.setAttribute(pair[0], String(v >>> 0 || pair[1]));
        });
    });
    accessor(global.HTMLTextAreaElement, "textLength", function () {
        return controlValue(this).length;
    });
    ["maxLength", "minLength"].forEach(function (name) {
        [global.HTMLInputElement, global.HTMLTextAreaElement].forEach(function (iface) {
            accessor(iface, name, function () {
                var n = parseInt(this.getAttribute(name.toLowerCase()), 10);
                return n >= 0 ? n : -1;
            }, function (v) {
                v = Number(v) | 0;
                if (v < 0) {
                    throw domException("The value provided is negative", "IndexSizeError");
                }
                this.setAttribute(name.toLowerCase(), String(v));
            });
        });
    });

    function isNumericInput(el) {
        return el.type === "number" || el.type === "range";
    }

    accessor(global.HTMLInputElement, "valueAsNumber", function () {
        var v = controlValue(this);
        if (isNumericInput(this)) {
            return v === "" ? NaN : Number(v);
        }
        if (this.type === "date" || this.type === "datetime-local" || this.type === "month") {
            var t = Date.parse(v + (this.type === "date" ? "T00:00:00Z" : this.type === "month" ? "-01T00:00:00Z" : "Z"));
            return isNaN(t) ? NaN : t;
        }
        return NaN;
    }, function (n) {
        n = Number(n);
        if (isNumericInput(this)) {
            this.value = isNaN(n) ? "" : String(n);
        } else if (this.type === "date") {
            this.value = isNaN(n) ? "" : new Date(n).toISOString().slice(0, 10);
        } else {
            throw domException("This input element does not support Number values.", "InvalidStateError");
        }
    });
    accessor(global.HTMLInputElement, "valueAsDate", function () {
        var n = this.valueAsNumber;
        return this.type === "date" && !isNaN(n) ? new Date(n) : null;
    }, function (d) {
        this.valueAsNumber = d === null ? NaN : d.getTime();
    });
    ["stepUp", "stepDown"].forEach(function (name, i) {
        define(global.HTMLInputElement.prototype, name, function (count) {
            if (!isNumericInput(this)) {
                throw domException("This form element is not steppable.", "InvalidStateError");
            }
            var step = numericValue(this, "step");
            if (isNaN(step) || step <= 0) {
                step = 1;
            }
            var value = Number(controlValue(this)) || 0;
            value += (i === 0 ? 1 : -1) * step * (count === undefined ? 1 : Number(count));
            var min = numericValue(this, "min"), max = numericValue(this, "max");
            if (!isNaN(min) && value < min) {
                value = min;
            }
            if (!isNaN(max) && value > max) {
                value = max;
            }
            this.value = String(value);
        });
    });

    if (typeof global.HTMLDialogElement === "function") {
        reflectBool(global.HTMLDialogElement, "open", "open");
        accessor(global.HTMLDialogElement, "returnValue", function () {
            return typeof this._returnValue === "string" ? this._returnValue : "";
        }, function (v) {
            define(this, "_returnValue", String(v));
        });
        define(global.HTMLDialogElement.prototype, "show", function () {
            this.setAttribute("open", "");
        });
        define(global.HTMLDialogElement.prototype, "showModal", function () {
            this.setAttribute("open", "");
        });
        define(global.HTMLDialogElement.prototype, "close", function (value) {
            if (!this.hasAttribute("open")) {
                return;
            }
            this.removeAttribute("open");
            if (value !== undefined) {
                this.returnValue = value;
            }
            var self = this;
            setTimeout(function () {
                self.dispatchEvent(new Event("close"));
            }, 0);
        });
    }

    if (typeof global.HTMLMediaElement === "function") {
        var mediaMethods = {
            play: function () {
                return Promise.reject(domException("Media playback is not supported", "NotSupportedError"));
            },
            pause: function () {
            },
            load: function () {
            },
            canPlayType: function () {
                return "";
            },
            fastSeek: function () {
            },
            addTextTrack: function () {
                throw domException("Text tracks are not supported", "NotSupportedError");
            }
        };
        Object.keys(mediaMethods).forEach(function (name) {
            define(global.HTMLMediaElement.prototype, name, mediaMethods[name]);
        });
        [["paused", true], ["ended", false], ["seeking", false], ["currentTime", 0], ["duration", NaN],
            ["readyState", 0], ["networkState", 0], ["volume", 1], ["muted", false], ["playbackRate", 1],
            ["defaultPlaybackRate", 1]].forEach(function (pair) {
            if (!(pair[0] in global.HTMLMediaElement.prototype)) {
                accessor(global.HTMLMediaElement, pair[0], function () {
                    var own = this["_" + pair[0]];
                    return own === undefined ? pair[1] : own;
                }, function (v) {
                    define(this, "_" + pair[0], v);
                });
            }
        });
        ["src", "poster"].forEach(function (name) {
            if (!(name in global.HTMLMediaElement.prototype)) {
                reflectString(global.HTMLMediaElement, name, name);
            }
        });
        ["autoplay", "loop", "controls"].forEach(function (name) {
            if (!(name in global.HTMLMediaElement.prototype)) {
                reflectBool(global.HTMLMediaElement, name, name);
            }
        });
    }

    function escapeMarkup(text, attr) {
        text = text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
        return attr ? text.replace(/"/g, "&quot;") : text;
    }

    function serializeNode(node) {
        switch (node.nodeType) {
        case 1:
            var name = node.tagName;
            if (node.namespaceURI === "http://www.w3.org/1999/xhtml") {
                name = node.localName;
            }
            var s = "<" + name;
            if (node.parentNode === null || node.parentNode.namespaceURI !== node.namespaceURI) {
                if (node.namespaceURI !== null && !node.hasAttribute("xmlns")) {
                    s += " xmlns=\"" + escapeMarkup(node.namespaceURI, true) + "\"";
                }
            }
            for (var i = 0; i < node.attributes.length; i++) {
                var a = node.attributes[i];
                s += " " + a.name + "=\"" + escapeMarkup(a.value, true) + "\"";
            }
            if (node.firstChild === null) {
                return s + "/>";
            }
            s += ">";
            for (var c = node.firstChild; c !== null; c = c.nextSibling) {
                s += serializeNode(c);
            }
            return s + "</" + name + ">";
        case 3:
            return escapeMarkup(node.data, false);
        case 4:
            return "<![CDATA[" + node.data + "]]>";
        case 7:
            return "<?" + node.target + " " + node.data + "?>";
        case 8:
            return "<!--" + node.data + "-->";
        case 10:
            return "<!DOCTYPE " + node.name + (node.publicId ? " PUBLIC \"" + node.publicId + "\"" : node.systemId ? " SYSTEM" : "") +
                (node.systemId ? " \"" + node.systemId + "\"" : "") + ">";
        case 9:
        case 11:
            var out = "";
            for (var k = node.firstChild; k !== null; k = k.nextSibling) {
                out += serializeNode(k);
            }
            return out;
        }
        return "";
    }

    function XMLSerializer() {
        if (new.target === undefined) {
            throw new TypeError("Failed to construct 'XMLSerializer': Please use the 'new' operator.");
        }
    }
    define(XMLSerializer.prototype, "serializeToString", function (node) {
        return serializeNode(requireNode(node));
    });
    define(global, "XMLSerializer", XMLSerializer);

    function defineEventInterface(name, base, fields) {
        var old = global[name];
        var proto = typeof old === "function" && old.prototype ? old.prototype : Object.create(base.prototype);
        function EventInterface(type, init) {
            if (new.target === undefined) {
                throw new TypeError("Failed to construct '" + name + "': Please use the 'new' operator.");
            }
            if (arguments.length < 1) {
                throw new TypeError("Failed to construct '" + name + "': 1 argument required, but only 0 present.");
            }
            var dict = init === undefined || init === null ? {} : init;
            var evt = Reflect.construct(base, [type, dict], new.target);
            Object.keys(fields).forEach(function (key) {
                var value = dict[key];
                Object.defineProperty(evt, key, {
                    value: value === undefined ? fields[key] : value,
                    enumerable: true,
                    configurable: true
                });
            });
            return evt;
        }
        EventInterface.prototype = proto;
        define(proto, "constructor", EventInterface);
        Object.defineProperty(EventInterface, "name", { value: name });
        define(global, name, EventInterface);
    }

    [
        ["FocusEvent", UIEvent, { relatedTarget: null }],
        ["CompositionEvent", UIEvent, { data: "" }],
        ["InputEvent", UIEvent, { data: null, isComposing: false, inputType: "", dataTransfer: null }],
        ["ErrorEvent", Event, { message: "", filename: "", lineno: 0, colno: 0, error: undefined }],
        ["MessageEvent", Event, { data: null, origin: "", lastEventId: "", source: null, ports: [] }],
        ["HashChangeEvent", Event, { oldURL: "", newURL: "" }],
        ["PopStateEvent", Event, { state: null }],
        ["ProgressEvent", Event, { lengthComputable: false, loaded: 0, total: 0 }],
        ["PromiseRejectionEvent", Event, { promise: undefined, reason: undefined }],
        ["StorageEvent", Event, { key: null, oldValue: null, newValue: null, url: "", storageArea: null }],
        ["SubmitEvent", Event, { submitter: null }],
        ["PageTransitionEvent", Event, { persisted: false }],
        ["CloseEvent", Event, { wasClean: false, code: 0, reason: "" }],
        ["WheelEvent", MouseEvent, { deltaX: 0, deltaY: 0, deltaZ: 0, deltaMode: 0 }],
        ["DragEvent", MouseEvent, { dataTransfer: null }]
    ].forEach(function (spec) {
        var existing = global[spec[0]];
        if (typeof existing === "function") {
            try {
                new existing("probe");
                return;
            } catch (e) {
            }
        }
        defineEventInterface(spec[0], spec[1], spec[2]);
    });

    [Element, CharacterData].forEach(function (iface) {
        if (!Object.getOwnPropertyDescriptor(iface.prototype, "nextElementSibling")) {
            defineGetter(iface.prototype, "nextElementSibling", function () {
                var n = this.nextSibling;
                while (n !== null && n.nodeType !== 1) {
                    n = n.nextSibling;
                }
                return n;
            });
        }
        if (!Object.getOwnPropertyDescriptor(iface.prototype, "previousElementSibling")) {
            defineGetter(iface.prototype, "previousElementSibling", function () {
                var n = this.previousSibling;
                while (n !== null && n.nodeType !== 1) {
                    n = n.previousSibling;
                }
                return n;
            });
        }
    });

    var legacyKeyCodes = {
        Backspace: 8, Tab: 9, Enter: 13, Shift: 16, Control: 17, Alt: 18, Pause: 19, CapsLock: 20,
        Escape: 27, " ": 32, PageUp: 33, PageDown: 34, End: 35, Home: 36, ArrowLeft: 37, ArrowUp: 38,
        ArrowRight: 39, ArrowDown: 40, Insert: 45, Delete: 46, Meta: 91, ContextMenu: 93,
        ";": 186, "=": 187, ",": 188, "-": 189, ".": 190, "/": 191, "`": 192, "[": 219, "\\": 220, "]": 221, "'": 222
    };

    function legacyKeyCode(evt) {
        var key = evt.key;
        if (typeof key !== "string") {
            return 0;
        }
        if (Object.prototype.hasOwnProperty.call(legacyKeyCodes, key)) {
            return legacyKeyCodes[key];
        }
        if (/^F([1-9]|1[0-2])$/.test(key)) {
            return 111 + Number(key.slice(1));
        }
        if (key.length === 1) {
            var c = key.toUpperCase();
            if (/[A-Z0-9]/.test(c)) {
                return c.charCodeAt(0);
            }
        }
        return 0;
    }

    function legacyCharCode(evt) {
        var key = evt.key;
        if (evt.type !== "keypress" || typeof key !== "string") {
            return 0;
        }
        if (key === "Enter") {
            return 13;
        }
        return Array.from(key).length === 1 ? key.codePointAt(0) : 0;
    }

    if (typeof global.KeyboardEvent === "function") {
        accessor(global.KeyboardEvent, "keyCode", function () {
            return this.type === "keypress" ? legacyCharCode(this) : legacyKeyCode(this);
        });
        accessor(global.KeyboardEvent, "charCode", function () {
            return legacyCharCode(this);
        });
        accessor(global.KeyboardEvent, "which", function () {
            return this.type === "keypress" ? legacyCharCode(this) : legacyKeyCode(this);
        });
        accessor(global.KeyboardEvent, "repeat", function () {
            return false;
        });
        accessor(global.KeyboardEvent, "isComposing", function () {
            return false;
        });
    }
    defineGetter(HTMLElement.prototype, "hidden", function () {
        return this.hasAttribute("hidden");
    }, function (v) {
        if (v) {
            this.setAttribute("hidden", "");
        } else {
            this.removeAttribute("hidden");
        }
    });
    defineGetter(HTMLElement.prototype, "innerText", function () {
        return this.textContent;
    }, function (v) {
        this.textContent = v === null || v === undefined ? "" : String(v);
    });

    var datasets = new WeakMap();
    function dataAttrName(prop) {
        return "data-" + prop.replace(/[A-Z]/g, function (c) { return "-" + c.toLowerCase(); });
    }
    defineGetter(HTMLElement.prototype, "dataset", function () {
        var el = this;
        var map = datasets.get(el);
        if (map) {
            return map;
        }
        map = new Proxy(Object.create(typeof DOMStringMap === "function" ? DOMStringMap.prototype : Object.prototype), {
            get: function (target, prop) {
                var v = typeof prop === "string" ? el.getAttribute(dataAttrName(prop)) : null;
                return v === null ? target[prop] : v;
            },
            set: function (target, prop, value) {
                if (typeof prop === "string") {
                    el.setAttribute(dataAttrName(prop), String(value));
                }
                return true;
            },
            has: function (target, prop) {
                return typeof prop === "string" && el.hasAttribute(dataAttrName(prop));
            },
            deleteProperty: function (target, prop) {
                if (typeof prop === "string") {
                    el.removeAttribute(dataAttrName(prop));
                }
                return true;
            },
            ownKeys: function () {
                return el.getAttributeNames().filter(function (n) {
                    return n.indexOf("data-") === 0;
                }).map(function (n) {
                    return n.slice(5).replace(/-([a-z])/g, function (m, c) { return c.toUpperCase(); });
                });
            },
            getOwnPropertyDescriptor: function (target, prop) {
                if (typeof prop !== "string") {
                    return undefined;
                }
                var v = el.getAttribute(dataAttrName(prop));
                return v === null ? undefined : { value: v, writable: true, enumerable: true, configurable: true };
            }
        });
        datasets.set(el, map);
        return map;
    });

    var clicking = new WeakSet();

    function labelControl(label) {
        var id = label.getAttribute("for");
        if (id !== null) {
            var target = label.ownerDocument.getElementById(id);
            return target !== null && /^(BUTTON|INPUT|METER|OUTPUT|PROGRESS|SELECT|TEXTAREA)$/.test(target.tagName) ? target : null;
        }
        return label.querySelector("button, input:not([type=hidden]), meter, output, progress, select, textarea");
    }

    define(HTMLElement.prototype, "click", function () {
        var tag = this.tagName;
        if (clicking.has(this) || (/^(BUTTON|INPUT|SELECT|TEXTAREA)$/.test(tag) && this.disabled)) {
            return;
        }
        var type = tag === "INPUT" ? String(this.getAttribute("type") || "").toLowerCase() :
            tag === "BUTTON" ? String(this.getAttribute("type") || "submit").toLowerCase() : "";
        var before = tag === "INPUT" && (type === "checkbox" || type === "radio") ? this.checked : null;
        if (before !== null) {
            this.checked = type === "checkbox" ? !before : true;
        }
        clicking.add(this);
        var go;
        try {
            var ev = typeof MouseEvent === "function" ?
                new MouseEvent("click", { bubbles: true, cancelable: true, composed: true, view: global, detail: 1 }) :
                new Event("click", { bubbles: true, cancelable: true });
            go = this.dispatchEvent(ev);
        } finally {
            clicking.delete(this);
        }
        if (before !== null) {
            if (!go) {
                this.checked = before;
            } else if (this.checked !== before) {
                this.dispatchEvent(new Event("input", { bubbles: true, composed: true }));
                this.dispatchEvent(new Event("change", { bubbles: true }));
            }
            return;
        }
        if (!go) {
            return;
        }
        if (tag === "A" || tag === "AREA") {
            if (this.hasAttribute("href")) {
                var href = native.resolve(this.getAttribute("href"));
                if (href !== null && !/^javascript:/i.test(href)) {
                    location.href = href;
                }
            }
        } else if (tag === "LABEL") {
            var control = labelControl(this);
            if (control !== null) {
                control.click();
            }
        } else if ((tag === "INPUT" || tag === "BUTTON") && this.form !== null) {
            if (type === "submit" || type === "image") {
                this.form.requestSubmit(this);
            } else if (type === "reset") {
                this.form.reset();
            }
        }
    });

    function DOMRect(x, y, width, height) {
        this.x = Number(x) || 0;
        this.y = Number(y) || 0;
        this.width = Number(width) || 0;
        this.height = Number(height) || 0;
        this.left = Math.min(this.x, this.x + this.width);
        this.top = Math.min(this.y, this.y + this.height);
        this.right = Math.max(this.x, this.x + this.width);
        this.bottom = Math.max(this.y, this.y + this.height);
    }
    define(DOMRect.prototype, "toJSON", function () {
        return { x: this.x, y: this.y, width: this.width, height: this.height, top: this.top, right: this.right, bottom: this.bottom, left: this.left };
    });
    defineMissing(global, "DOMRect", DOMRect);

    var historyState = null;
    var historyObject = {};
    Object.defineProperty(historyObject, "length", { get: function () { return 1; }, enumerable: true });
    Object.defineProperty(historyObject, "state", { get: function () { return historyState; }, enumerable: true });
    Object.defineProperty(historyObject, "scrollRestoration", { value: "auto", writable: true, enumerable: true });
    function changeState(state, title, url) {
        if (url !== undefined && url !== null) {
            var target = native.resolve(String(url));
            if (target === null || native.origin(target) !== native.origin(location.href)) {
                throw domException("The URL is not of the same origin as the document.", "SecurityError");
            }
        }
        historyState = state === undefined ? null : structuredCloneImpl(state);
    }
    define(historyObject, "pushState", changeState);
    define(historyObject, "replaceState", changeState);
    define(historyObject, "back", function () {});
    define(historyObject, "forward", function () {});
    define(historyObject, "go", function (delta) {
        if (!delta) {
            location.reload();
        }
    });
    define(historyObject, Symbol.toStringTag, "History");
    defineGetter(Window.prototype, "history", function () {
        return historyObject;
    });

    var screenObject = {};
    ["width", "availWidth"].forEach(function (name) {
        Object.defineProperty(screenObject, name, { get: function () { return global.outerWidth || global.innerWidth || 1024; }, enumerable: true });
    });
    ["height", "availHeight"].forEach(function (name) {
        Object.defineProperty(screenObject, name, { get: function () { return global.outerHeight || global.innerHeight || 768; }, enumerable: true });
    });
    define(screenObject, "colorDepth", 24);
    define(screenObject, "pixelDepth", 24);
    define(screenObject, Symbol.toStringTag, "Screen");
    defineGetter(Window.prototype, "screen", function () {
        return screenObject;
    });

    function languages() {
        return native.language().split(",").map(function (part) {
            return part.split(";")[0].trim();
        }).filter(function (part) {
            return part.length > 0;
        });
    }
    defineGetter(Navigator.prototype, "language", function () {
        return languages()[0] || "en";
    });
    defineGetter(Navigator.prototype, "languages", function () {
        var list = languages();
        return Object.freeze(list.length ? list : ["en"]);
    });
    defineGetter(Navigator.prototype, "cookieEnabled", function () {
        return true;
    });
    defineGetter(Navigator.prototype, "onLine", function () {
        return true;
    });
    defineGetter(Navigator.prototype, "hardwareConcurrency", function () {
        return 1;
    });

    var integerArrays = [Int8Array, Uint8Array, Uint8ClampedArray, Int16Array, Uint16Array, Int32Array, Uint32Array, BigInt64Array, BigUint64Array];
    var cryptoObject = {};
    define(cryptoObject, "getRandomValues", function (array) {
        if (!integerArrays.some(function (t) { return array instanceof t; })) {
            throw domException("The data is not an integer typed array.", "TypeMismatchError");
        }
        if (array.byteLength > 65536) {
            throw domException("The requested length exceeds 65536 bytes.", "QuotaExceededError");
        }
        new Uint8Array(array.buffer, array.byteOffset, array.byteLength).set(new Uint8Array(native.random(array.byteLength)));
        return array;
    });
    define(cryptoObject, "randomUUID", function () {
        var b = new Uint8Array(native.random(16));
        b[6] = (b[6] & 0x0f) | 0x40;
        b[8] = (b[8] & 0x3f) | 0x80;
        var h = Array.from(b, function (x) { return (x + 256).toString(16).slice(1); }).join("");
        return h.slice(0, 8) + "-" + h.slice(8, 12) + "-" + h.slice(12, 16) + "-" + h.slice(16, 20) + "-" + h.slice(20);
    });
    define(cryptoObject, Symbol.toStringTag, "Crypto");
    defineGetter(Window.prototype, "crypto", function () {
        return cryptoObject;
    });

    function structuredCloneImpl(value, seen) {
        if (value === null || typeof value !== "object") {
            if (typeof value === "function" || typeof value === "symbol") {
                throw domException(String(value) + " could not be cloned.", "DataCloneError");
            }
            return value;
        }
        seen = seen || new Map();
        if (seen.has(value)) {
            return seen.get(value);
        }
        var copy;
        if (value instanceof Date) {
            copy = new Date(value.getTime());
        } else if (value instanceof RegExp) {
            copy = new RegExp(value.source, value.flags);
        } else if (value instanceof ArrayBuffer) {
            copy = value.slice(0);
        } else if (ArrayBuffer.isView(value)) {
            copy = new value.constructor(structuredCloneImpl(value.buffer, seen), value.byteOffset, value instanceof DataView ? value.byteLength : value.length);
        } else if (value instanceof Map) {
            copy = new Map();
            seen.set(value, copy);
            value.forEach(function (v, k) {
                copy.set(structuredCloneImpl(k, seen), structuredCloneImpl(v, seen));
            });
            return copy;
        } else if (value instanceof Set) {
            copy = new Set();
            seen.set(value, copy);
            value.forEach(function (v) {
                copy.add(structuredCloneImpl(v, seen));
            });
            return copy;
        } else if (value instanceof Error) {
            copy = new Error(value.message);
            copy.name = value.name;
        } else if (Array.isArray(value)) {
            copy = [];
            seen.set(value, copy);
            value.forEach(function (v, i) {
                copy[i] = structuredCloneImpl(v, seen);
            });
            return copy;
        } else if (typeof Blob === "function" && value instanceof Blob) {
            return value;
        } else if (value instanceof Node || value instanceof Promise || value instanceof WeakMap || value instanceof WeakSet) {
            throw domException("The object could not be cloned.", "DataCloneError");
        } else {
            copy = {};
            seen.set(value, copy);
            Object.keys(value).forEach(function (k) {
                copy[k] = structuredCloneImpl(value[k], seen);
            });
            return copy;
        }
        seen.set(value, copy);
        return copy;
    }
    defineMissing(global, "structuredClone", function structuredClone(value) {
        return structuredCloneImpl(value);
    });

    var idleCallbacks = new Map();
    var nextIdle = 1;
    defineMissing(Window.prototype, "requestIdleCallback", function (fn, options) {
        var id = nextIdle++;
        var start = Date.now();
        idleCallbacks.set(id, global.setTimeout(function () {
            idleCallbacks.delete(id);
            fn({
                didTimeout: false,
                timeRemaining: function () {
                    return Math.max(0, 50 - (Date.now() - start));
                }
            });
        }, 1));
        return id;
    });
    defineMissing(Window.prototype, "cancelIdleCallback", function (id) {
        if (idleCallbacks.has(id)) {
            global.clearTimeout(idleCallbacks.get(id));
            idleCallbacks.delete(id);
        }
    });

    function Image(width, height) {
        var img = document.createElement("img");
        if (width !== undefined) {
            img.setAttribute("width", String(width));
        }
        if (height !== undefined) {
            img.setAttribute("height", String(height));
        }
        return img;
    }
    Image.prototype = HTMLImageElement.prototype;
    defineMissing(global, "Image", Image);
    function Option(text, value, defaultSelected, selected) {
        var opt = document.createElement("option");
        if (text !== undefined) {
            opt.textContent = String(text);
        }
        if (value !== undefined) {
            opt.setAttribute("value", String(value));
        }
        if (defaultSelected) {
            opt.setAttribute("selected", "");
        }
        if (selected) {
            opt.selected = true;
        }
        return opt;
    }
    Option.prototype = HTMLOptionElement.prototype;
    defineMissing(global, "Option", Option);

    function ObserverEntries(callback, make) {
        this._callback = callback;
        this._targets = [];
        this._make = make;
    }
    function makeObserver(name, make) {
        var Ctor = function (callback) {
            if (!new.target) {
                throw new TypeError("Constructor " + name + " requires 'new'");
            }
            if (typeof callback !== "function") {
                throw new TypeError("The callback provided is not a function.");
            }
            ObserverEntries.call(this, callback, make);
        };
        define(Ctor.prototype, "observe", function (target) {
            var self = this;
            if (this._targets.indexOf(target) >= 0) {
                return;
            }
            this._targets.push(target);
            Promise.resolve().then(function () {
                if (self._targets.indexOf(target) >= 0) {
                    self._callback.call(self, [self._make(target)], self);
                }
            });
        });
        define(Ctor.prototype, "unobserve", function (target) {
            this._targets = this._targets.filter(function (t) { return t !== target; });
        });
        define(Ctor.prototype, "disconnect", function () {
            this._targets = [];
        });
        define(Ctor.prototype, "takeRecords", function () {
            return [];
        });
        define(Ctor.prototype, Symbol.toStringTag, name);
        Object.defineProperty(Ctor, "name", { value: name });
        defineMissing(global, name, Ctor);
    }
    makeObserver("IntersectionObserver", function (target) {
        var rect = new DOMRect(0, 0, 0, 0);
        return { target: target, isIntersecting: true, intersectionRatio: 1, boundingClientRect: rect, intersectionRect: rect, rootBounds: null, time: Date.now() };
    });
    makeObserver("ResizeObserver", function (target) {
        var rect = new DOMRect(0, 0, 0, 0);
        return { target: target, contentRect: rect, borderBoxSize: [{ inlineSize: 0, blockSize: 0 }], contentBoxSize: [{ inlineSize: 0, blockSize: 0 }] };
    });

    function blobBytes(parts) {
        var chunks = [];
        var total = 0;
        (parts || []).forEach(function (part) {
            var bytes;
            if (part instanceof Blob) {
                bytes = part._bytes;
            } else if (part instanceof ArrayBuffer) {
                bytes = new Uint8Array(part.slice(0));
            } else if (ArrayBuffer.isView(part)) {
                bytes = new Uint8Array(part.buffer.slice(part.byteOffset, part.byteOffset + part.byteLength));
            } else {
                bytes = new Uint8Array(native.utf8Encode(String(part)));
            }
            chunks.push(bytes);
            total += bytes.length;
        });
        var out = new Uint8Array(total);
        var offset = 0;
        chunks.forEach(function (c) {
            out.set(c, offset);
            offset += c.length;
        });
        return out;
    }
    function Blob(parts, options) {
        if (!new.target) {
            throw new TypeError("Constructor Blob requires 'new'");
        }
        define(this, "_bytes", blobBytes(parts));
        define(this, "_type", options && options.type ? String(options.type).toLowerCase() : "");
    }
    Object.defineProperty(Blob.prototype, "size", { get: function () { return this._bytes.length; }, configurable: true });
    Object.defineProperty(Blob.prototype, "type", { get: function () { return this._type; }, configurable: true });
    define(Blob.prototype, "arrayBuffer", function () {
        return Promise.resolve(this._bytes.slice().buffer);
    });
    define(Blob.prototype, "bytes", function () {
        return Promise.resolve(this._bytes.slice());
    });
    define(Blob.prototype, "text", function () {
        return Promise.resolve(native.utf8Decode(this._bytes));
    });
    define(Blob.prototype, "slice", function (start, end, type) {
        return new Blob([this._bytes.slice(start, end)], { type: type || "" });
    });
    define(Blob.prototype, Symbol.toStringTag, "Blob");
    function File(parts, name, options) {
        if (!new.target) {
            throw new TypeError("Constructor File requires 'new'");
        }
        var blob = Reflect.construct(Blob, [parts, options], new.target);
        define(blob, "_name", String(name));
        define(blob, "_lastModified", options && options.lastModified !== undefined ? Number(options.lastModified) : Date.now());
        return blob;
    }
    Object.setPrototypeOf(File.prototype, Blob.prototype);
    Object.defineProperty(File.prototype, "name", { get: function () { return this._name; }, configurable: true });
    Object.defineProperty(File.prototype, "lastModified", { get: function () { return this._lastModified; }, configurable: true });
    define(File.prototype, Symbol.toStringTag, "File");
    defineMissing(global, "Blob", Blob);
    defineMissing(global, "File", File);

    var perfEntries = [];
    var perfObservers = [];

    function PerformanceEntry(name, type, start, duration, detail) {
        define(this, "name", String(name));
        define(this, "entryType", type);
        define(this, "startTime", start);
        define(this, "duration", duration);
        define(this, "detail", detail === undefined ? null : detail);
    }
    define(PerformanceEntry.prototype, "toJSON", function () {
        return { name: this.name, entryType: this.entryType, startTime: this.startTime, duration: this.duration };
    });

    function perfRecord(entry) {
        perfEntries.push(entry);
        perfObservers.forEach(function (o) {
            if (o._types.indexOf(entry.entryType) >= 0) {
                o._buffer.push(entry);
                if (!o._queued) {
                    o._queued = true;
                    queueMicrotask(function () {
                        o._queued = false;
                        var list = o._buffer;
                        o._buffer = [];
                        if (list.length > 0) {
                            o._callback({
                                getEntries: function () { return list.slice(); },
                                getEntriesByType: function (t) { return list.filter(function (e) { return e.entryType === t; }); },
                                getEntriesByName: function (n, t) { return list.filter(function (e) { return e.name === n && (t === undefined || e.entryType === t); }); }
                            }, o);
                        }
                    });
                }
            }
        });
        return entry;
    }

    function perfTime(value) {
        if (typeof value === "number") {
            return value;
        }
        if (typeof value === "string") {
            if (/^(navigationStart|unloadEvent(Start|End)|redirect(Start|End)|fetchStart|domainLookup(Start|End)|connect(Start|End)|secureConnectionStart|request(Start)|response(Start|End)|domLoading|domInteractive|domContentLoadedEvent(Start|End)|domComplete|loadEvent(Start|End))$/.test(value)) {
                return 0;
            }
            for (var i = perfEntries.length - 1; i >= 0; i--) {
                if (perfEntries[i].entryType === "mark" && perfEntries[i].name === value) {
                    return perfEntries[i].startTime;
                }
            }
            throw domException("The mark '" + value + "' does not exist.", "SyntaxError");
        }
        return undefined;
    }

    if (typeof performance === "object" && performance !== null) {
        defineMissing(performance, "mark", function (name, options) {
            var start = options && options.startTime !== undefined ? Number(options.startTime) : performance.now();
            return perfRecord(new PerformanceEntry(name, "mark", start, 0, options && options.detail));
        });
        defineMissing(performance, "measure", function (name, startOrOptions, endMark) {
            var start = 0, end = performance.now(), detail;
            if (startOrOptions !== null && typeof startOrOptions === "object") {
                detail = startOrOptions.detail;
                if (startOrOptions.start !== undefined) {
                    start = perfTime(startOrOptions.start);
                }
                if (startOrOptions.end !== undefined) {
                    end = perfTime(startOrOptions.end);
                } else if (startOrOptions.duration !== undefined) {
                    end = start + Number(startOrOptions.duration);
                }
            } else {
                if (startOrOptions !== undefined) {
                    start = perfTime(startOrOptions);
                }
                if (endMark !== undefined) {
                    end = perfTime(endMark);
                }
            }
            return perfRecord(new PerformanceEntry(name, "measure", start, end - start, detail));
        });
        defineMissing(performance, "getEntries", function () {
            return perfEntries.slice();
        });
        defineMissing(performance, "getEntriesByType", function (type) {
            return perfEntries.filter(function (e) { return e.entryType === type; });
        });
        defineMissing(performance, "getEntriesByName", function (name, type) {
            return perfEntries.filter(function (e) {
                return e.name === name && (type === undefined || e.entryType === type);
            });
        });
        ["Marks", "Measures"].forEach(function (kind) {
            var type = kind === "Marks" ? "mark" : "measure";
            defineMissing(performance, "clear" + kind, function (name) {
                perfEntries = perfEntries.filter(function (e) {
                    return e.entryType !== type || (name !== undefined && e.name !== name);
                });
            });
        });
        defineMissing(performance, "clearResourceTimings", function () {
        });
        defineMissing(performance, "setResourceTimingBufferSize", function () {
        });
        defineMissing(performance, "toJSON", function () {
            return { timeOrigin: performance.timeOrigin };
        });
    }

    function PerformanceObserver(callback) {
        if (!new.target) {
            throw new TypeError("Constructor PerformanceObserver requires 'new'");
        }
        if (typeof callback !== "function") {
            throw new TypeError("Failed to construct 'PerformanceObserver': parameter 1 is not a function.");
        }
        define(this, "_callback", callback);
        define(this, "_types", []);
        define(this, "_buffer", []);
        define(this, "_queued", false);
    }
    define(PerformanceObserver.prototype, "observe", function (options) {
        var types = options && options.entryTypes ? options.entryTypes : options && options.type ? [options.type] : [];
        var self = this;
        types.forEach(function (t) {
            if (self._types.indexOf(t) < 0) {
                self._types.push(t);
            }
        });
        if (perfObservers.indexOf(this) < 0) {
            perfObservers.push(this);
        }
        if (options && options.buffered) {
            perfEntries.forEach(function (e) {
                if (self._types.indexOf(e.entryType) >= 0) {
                    self._buffer.push(e);
                }
            });
        }
    });
    define(PerformanceObserver.prototype, "disconnect", function () {
        var i = perfObservers.indexOf(this);
        if (i >= 0) {
            perfObservers.splice(i, 1);
        }
    });
    define(PerformanceObserver.prototype, "takeRecords", function () {
        var list = this._buffer;
        this._buffer = [];
        return list;
    });
    define(PerformanceObserver, "supportedEntryTypes", Object.freeze(["mark", "measure"]));
    defineMissing(global, "PerformanceObserver", PerformanceObserver);
    defineMissing(global, "PerformanceEntry", PerformanceEntry);

    var importMap = { imports: {}, scopes: {} };
    var moduleTexts = new Map();
    var moduleLoads = new Map();
    var moduleQueue = [];
    var modulesReady = false;
    var inlineModules = 0;

    function absoluteURL(text, base) {
        try {
            return new URL(text, base).href;
        } catch (e) {
            return null;
        }
    }

    function mapSpecifier(map, spec) {
        if (Object.prototype.hasOwnProperty.call(map, spec)) {
            return map[spec];
        }
        var best = null;
        Object.keys(map).forEach(function (key) {
            if (key.charAt(key.length - 1) === "/" && spec.indexOf(key) === 0 &&
                (best === null || key.length > best.length)) {
                best = key;
            }
        });
        return best === null ? null : map[best] + spec.slice(best.length);
    }

    function resolveModule(spec, base) {
        if (!/^[a-z][a-z0-9+.-]*:/i.test(base)) {
            base = location.href;
        }
        var relative = /^(\/|\.\/|\.\.\/)/.test(spec);
        var key = relative ? absoluteURL(spec, base) : (absoluteURL(spec) || spec);
        var scopes = Object.keys(importMap.scopes).sort(function (a, b) {
            return b.length - a.length;
        });
        for (var i = 0; i < scopes.length; i++) {
            if (base.indexOf(scopes[i]) === 0) {
                var scoped = mapSpecifier(importMap.scopes[scopes[i]], key);
                if (scoped !== null) {
                    return scoped;
                }
            }
        }
        var mapped = mapSpecifier(importMap.imports, key);
        if (mapped !== null) {
            return mapped;
        }
        if (relative || absoluteURL(spec) !== null) {
            return key;
        }
        throw new TypeError("Failed to resolve module specifier \"" + spec + "\"");
    }

    function addImportMap(text) {
        var map;
        try {
            map = JSON.parse(text);
        } catch (e) {
            reportError(e);
            return;
        }
        var base = location.href;
        function normalise(entries) {
            var out = {};
            Object.keys(entries || {}).forEach(function (key) {
                var target = absoluteURL(String(entries[key]), base);
                var name = /^(\/|\.\/|\.\.\/)/.test(key) ? absoluteURL(key, base) : key;
                if (target !== null && name !== null) {
                    out[name] = target;
                }
            });
            return out;
        }
        Object.assign(importMap.imports, normalise(map.imports));
        Object.keys(map.scopes || {}).forEach(function (scope) {
            var url = absoluteURL(scope, base);
            if (url !== null) {
                importMap.scopes[url] = Object.assign(importMap.scopes[url] || {}, normalise(map.scopes[scope]));
            }
        });
    }

    function scanImports(text) {
        var specs = [];
        var patterns = [
            /(?:^|[^.\w$])(?:import|export)\s*(?:[\w$*{}\s,]+?\s*from\s*)(["'])([^"'\n]+)\1/g,
            /(?:^|[^.\w$])import\s*(["'])([^"'\n]+)\1/g
        ];
        patterns.forEach(function (re) {
            var m;
            while ((m = re.exec(text)) !== null) {
                specs.push(m[2]);
            }
        });
        return specs;
    }

    function moduleSource(url, inlineText) {
        if (moduleLoads.has(url)) {
            return moduleLoads.get(url);
        }
        var source = inlineText !== undefined ? Promise.resolve(inlineText) :
            fetch(url, { credentials: "same-origin" }).then(function (r) {
                if (!r.ok) {
                    throw new TypeError("Failed to fetch module " + url);
                }
                return r.text();
            });
        source = source.then(function (text) {
            moduleTexts.set(url, text);
            return text;
        });
        moduleLoads.set(url, source);
        return source;
    }

    function loadModuleGraph(url, inlineText) {
        return new Promise(function (resolve, reject) {
            var visited = new Set();
            var pending = 0;
            var entryError = null;
            function visit(target, text) {
                if (visited.has(target)) {
                    return;
                }
                visited.add(target);
                pending++;
                moduleSource(target, text).then(function (source) {
                    scanImports(source).forEach(function (spec) {
                        try {
                            visit(resolveModule(spec, target));
                        } catch (e) {
                        }
                    });
                }, function (e) {
                    if (target === url) {
                        entryError = e;
                    }
                }).then(function () {
                    if (--pending === 0) {
                        if (entryError !== null) {
                            reject(entryError);
                        } else {
                            resolve();
                        }
                    }
                });
            }
            visit(url, inlineText);
        });
    }

    function fireScriptEvent(node, type) {
        try {
            node.dispatchEvent(new Event(type));
        } catch (e) {
        }
    }

    function runModuleEntry(entry) {
        if (entry.state === "failed") {
            reportError(entry.error);
            fireScriptEvent(entry.node, "error");
            return;
        }
        native.runModule(entry.url).then(function () {
            fireScriptEvent(entry.node, "load");
        }, function (e) {
            reportError(e);
        });
    }

    function runModules() {
        modulesReady = true;
        while (moduleQueue.length > 0 && moduleQueue[0].state !== "loading") {
            runModuleEntry(moduleQueue.shift());
        }
    }

    function loadModuleScript(node, src, text) {
        var url = src !== null ? src : location.href.replace(/#.*$/, "") + "#module-" + (++inlineModules);
        var entry = { node: node, url: url, state: "loading", error: null };
        var isAsync = src !== null && node !== null && node.hasAttribute("async");
        if (!isAsync) {
            moduleQueue.push(entry);
        }
        loadModuleGraph(url, src === null ? (text === null ? "" : text) : undefined).then(function () {
            entry.state = "ready";
        }, function (e) {
            entry.state = "failed";
            entry.error = e;
        }).then(function () {
            native.moduleLoaded();
            if (isAsync) {
                runModuleEntry(entry);
            } else if (modulesReady) {
                runModules();
            }
        });
    }

    var cssNamespace = global.CSS !== undefined && global.CSS !== null ? global.CSS : {};
    defineMissing(cssNamespace, "supports", function (property, value) {
        if (arguments.length >= 2) {
            return native.cssSupports("(" + String(property) + ": " + String(value) + ")");
        }
        var text = String(property);
        return native.cssSupports(text) || native.cssSupports("(" + text + ")");
    });
    defineMissing(cssNamespace, "escape", function (value) {
        var string = String(value);
        var first = string.charCodeAt(0);
        var result = "";
        if (string.length === 1 && first === 0x2d) {
            return "\\" + string;
        }
        for (var i = 0; i < string.length; i++) {
            var code = string.charCodeAt(i);
            if (code === 0) {
                result += "\ufffd";
            } else if ((code >= 1 && code <= 0x1f) || code === 0x7f ||
                (i === 0 && code >= 0x30 && code <= 0x39) ||
                (i === 1 && code >= 0x30 && code <= 0x39 && first === 0x2d)) {
                result += "\\" + code.toString(16) + " ";
            } else if (code >= 0x80 || code === 0x2d || code === 0x5f ||
                (code >= 0x30 && code <= 0x39) || (code >= 0x41 && code <= 0x5a) ||
                (code >= 0x61 && code <= 0x7a)) {
                result += string.charAt(i);
            } else {
                result += "\\" + string.charAt(i);
            }
        }
        return result;
    });
    defineMissing(global, "CSS", cssNamespace);

    var blobRegistry = new Map();
    var blobCounter = 0;
    defineMissing(URL, "createObjectURL", function (blob) {
        if (!(blob instanceof Blob)) {
            throw new TypeError("Failed to execute 'createObjectURL' on 'URL': parameter 1 is not of type 'Blob'.");
        }
        var origin = native.origin(location.href);
        blobCounter++;
        var id = [8, 4, 4, 4, 12].map(function (n) {
            var out = "";
            for (var i = 0; i < n; i++) {
                out += Math.floor(Math.random() * 16).toString(16);
            }
            return out;
        }).join("-");
        var url = "blob:" + (origin === "null" ? "null" : origin) + "/" + id;
        blobRegistry.set(url, blob);
        return url;
    });
    defineMissing(URL, "revokeObjectURL", function (url) {
        blobRegistry.delete(String(url));
    });

    function SimpleEventTarget() {
    }
    define(SimpleEventTarget.prototype, "addEventListener", function (type, fn) {
        if (typeof fn !== "function" && !(fn && typeof fn.handleEvent === "function")) {
            return;
        }
        var map = this._listeners || (define(this, "_listeners", {}), this._listeners);
        (map[type] || (map[type] = [])).push(fn);
    });
    define(SimpleEventTarget.prototype, "removeEventListener", function (type, fn) {
        var list = this._listeners && this._listeners[type];
        if (list) {
            this._listeners[type] = list.filter(function (f) { return f !== fn; });
        }
    });
    define(SimpleEventTarget.prototype, "_emit", function (type, extra) {
        var self = this;
        var ev = { type: type, target: this, currentTarget: this, bubbles: false, cancelable: false, defaultPrevented: false, timeStamp: Date.now(), preventDefault: function () {}, stopPropagation: function () {} };
        if (extra) {
            Object.keys(extra).forEach(function (k) {
                ev[k] = extra[k];
            });
        }
        var handlers = [];
        if (typeof this["on" + type] === "function") {
            handlers.push(this["on" + type]);
        }
        handlers = handlers.concat((this._listeners && this._listeners[type]) || []);
        handlers.forEach(function (fn) {
            try {
                if (typeof fn === "function") {
                    fn.call(self, ev);
                } else {
                    fn.handleEvent(ev);
                }
            } catch (e) {
                reportPreludeError(e);
            }
        });
    });
    function reportPreludeError(e) {
        Promise.reject(e);
    }

    function FileReader() {
        if (!new.target) {
            throw new TypeError("Constructor FileReader requires 'new'");
        }
        define(this, "_state", 0);
        define(this, "_result", null);
        ["onload", "onerror", "onloadend", "onloadstart", "onprogress", "onabort"].forEach(function (n) {
            this[n] = null;
        }, this);
    }
    Object.setPrototypeOf(FileReader.prototype, SimpleEventTarget.prototype);
    Object.defineProperty(FileReader.prototype, "readyState", { get: function () { return this._state; }, configurable: true });
    Object.defineProperty(FileReader.prototype, "result", { get: function () { return this._result; }, configurable: true });
    Object.defineProperty(FileReader.prototype, "error", { get: function () { return null; }, configurable: true });
    function readBlob(reader, blob, convert) {
        if (!(blob instanceof Blob)) {
            throw new TypeError("The argument is not a Blob.");
        }
        reader._state = 1;
        reader._emit("loadstart");
        Promise.resolve().then(function () {
            reader._result = convert(blob);
            reader._state = 2;
            reader._emit("load");
            reader._emit("loadend");
        });
    }
    define(FileReader.prototype, "readAsText", function (blob) {
        readBlob(this, blob, function (b) { return native.utf8Decode(b._bytes); });
    });
    define(FileReader.prototype, "readAsArrayBuffer", function (blob) {
        readBlob(this, blob, function (b) { return b._bytes.slice().buffer; });
    });
    define(FileReader.prototype, "readAsDataURL", function (blob) {
        readBlob(this, blob, function (b) {
            var s = "";
            for (var i = 0; i < b._bytes.length; i++) {
                s += String.fromCharCode(b._bytes[i]);
            }
            return "data:" + (b.type || "application/octet-stream") + ";base64," + btoa(s);
        });
    });
    define(FileReader.prototype, "abort", function () {
        this._state = 2;
    });
    defineMissing(global, "FileReader", FileReader);

    function formControlValues(form) {
        var entries = [];
        Array.from(form.getElementsByTagName("*")).forEach(function (el) {
            var name = el.getAttribute("name");
            var tag = el.tagName;
            if (!name || el.hasAttribute("disabled") || (tag !== "INPUT" && tag !== "SELECT" && tag !== "TEXTAREA")) {
                return;
            }
            var type = String(el.getAttribute("type") || "").toLowerCase();
            if (tag === "INPUT" && ["submit", "button", "reset", "image", "file"].indexOf(type) >= 0) {
                return;
            }
            if ((type === "checkbox" || type === "radio") && !el.checked) {
                return;
            }
            if (tag === "SELECT") {
                Array.from(el.getElementsByTagName("option")).forEach(function (opt) {
                    if (opt.selected) {
                        entries.push([name, opt.hasAttribute("value") ? opt.getAttribute("value") : opt.textContent]);
                    }
                });
                return;
            }
            entries.push([name, type === "checkbox" || type === "radio" ? (el.hasAttribute("value") ? el.getAttribute("value") : "on") : String(el.value === undefined ? "" : el.value)]);
        });
        return entries;
    }
    function FormData(form) {
        if (!new.target) {
            throw new TypeError("Constructor FormData requires 'new'");
        }
        define(this, "_entries", form ? formControlValues(form) : []);
    }
    function formValue(value, filename) {
        if (value instanceof Blob) {
            return value instanceof File && filename === undefined ? value : new File([value], filename !== undefined ? String(filename) : "blob", { type: value.type });
        }
        return String(value);
    }
    define(FormData.prototype, "append", function (name, value, filename) {
        this._entries.push([String(name), formValue(value, filename)]);
    });
    define(FormData.prototype, "set", function (name, value, filename) {
        name = String(name);
        var v = formValue(value, filename);
        var i = this._entries.findIndex(function (e) { return e[0] === name; });
        this._entries = this._entries.filter(function (e, j) { return e[0] !== name || j === i; });
        if (i >= 0) {
            this._entries[this._entries.findIndex(function (e) { return e[0] === name; })][1] = v;
        } else {
            this._entries.push([name, v]);
        }
    });
    define(FormData.prototype, "get", function (name) {
        var e = this._entries.find(function (e) { return e[0] === String(name); });
        return e ? e[1] : null;
    });
    define(FormData.prototype, "getAll", function (name) {
        return this._entries.filter(function (e) { return e[0] === String(name); }).map(function (e) { return e[1]; });
    });
    define(FormData.prototype, "has", function (name) {
        return this._entries.some(function (e) { return e[0] === String(name); });
    });
    define(FormData.prototype, "delete", function (name) {
        this._entries = this._entries.filter(function (e) { return e[0] !== String(name); });
    });
    define(FormData.prototype, "entries", function () {
        return this._entries.map(function (e) { return [e[0], e[1]]; })[Symbol.iterator]();
    });
    define(FormData.prototype, "keys", function () {
        return this._entries.map(function (e) { return e[0]; })[Symbol.iterator]();
    });
    define(FormData.prototype, "values", function () {
        return this._entries.map(function (e) { return e[1]; })[Symbol.iterator]();
    });
    define(FormData.prototype, "forEach", function (fn, thisArg) {
        var self = this;
        this._entries.slice().forEach(function (e) {
            fn.call(thisArg, e[1], e[0], self);
        });
    });
    define(FormData.prototype, Symbol.iterator, FormData.prototype.entries);
    define(FormData.prototype, Symbol.toStringTag, "FormData");
    defineMissing(global, "FormData", FormData);

    function encodeMultipart(formData) {
        var boundary = "----TinySurfFormBoundary" + Array.from(new Uint8Array(native.random(8)), function (x) { return (x + 256).toString(16).slice(1); }).join("");
        var parts = [];
        formData._entries.forEach(function (e) {
            var head = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + e[0].replace(/"/g, "%22") + "\"";
            if (e[1] instanceof Blob) {
                head += "; filename=\"" + (e[1].name || "blob").replace(/"/g, "%22") + "\"\r\nContent-Type: " + (e[1].type || "application/octet-stream");
                parts.push(head + "\r\n\r\n", e[1], "\r\n");
            } else {
                parts.push(head + "\r\n\r\n" + e[1] + "\r\n");
            }
        });
        parts.push("--" + boundary + "--\r\n");
        return { bytes: blobBytes(parts), type: "multipart/form-data; boundary=" + boundary };
    }

    var intlLocale = function () {
        return languages()[0] || "en-US";
    };
    function groupDigits(text) {
        var neg = text[0] === "-";
        var body = neg ? text.slice(1) : text;
        var parts = body.split(".");
        parts[0] = parts[0].replace(/\B(?=(\d{3})+(?!\d))/g, ",");
        return (neg ? "-" : "") + parts.join(".");
    }
    function NumberFormat(locale, options) {
        if (!(this instanceof NumberFormat)) {
            return new NumberFormat(locale, options);
        }
        this._options = options || {};
    }
    define(NumberFormat.prototype, "format", function (n) {
        var o = this._options;
        var value = Number(n);
        if (!isFinite(value)) {
            return String(value);
        }
        if (o.style === "percent") {
            value *= 100;
        }
        var min = o.minimumFractionDigits !== undefined ? o.minimumFractionDigits : (o.style === "currency" ? 2 : 0);
        var max = o.maximumFractionDigits !== undefined ? Math.max(o.maximumFractionDigits, min) : Math.max(min, o.style === "currency" ? 2 : (o.style === "percent" ? 0 : 3));
        var text = value.toFixed(max);
        if (max > min) {
            text = text.replace(new RegExp("(\\.\\d{" + min + "}\\d*?)0+$"), "$1").replace(/\.$/, "");
        }
        if (o.useGrouping !== false) {
            text = groupDigits(text);
        }
        if (o.style === "percent") {
            return text + "%";
        }
        if (o.style === "currency") {
            var symbols = { USD: "$", EUR: "€", GBP: "£", JPY: "¥" };
            var c = String(o.currency || "").toUpperCase();
            return (symbols[c] || c + " ") + text;
        }
        return text;
    });
    define(NumberFormat.prototype, "formatToParts", function (n) {
        return [{ type: "literal", value: this.format(n) }];
    });
    define(NumberFormat.prototype, "resolvedOptions", function () {
        return Object.assign({ locale: intlLocale(), numberingSystem: "latn", style: "decimal" }, this._options);
    });
    function DateTimeFormat(locale, options) {
        if (!(this instanceof DateTimeFormat)) {
            return new DateTimeFormat(locale, options);
        }
        this._options = options || {};
    }
    var monthNames = ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"];
    var dayNames = ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"];
    define(DateTimeFormat.prototype, "format", function (date) {
        var d = date === undefined ? new Date() : new Date(date);
        var o = this._options;
        if (isNaN(d.getTime())) {
            throw new RangeError("Invalid time value");
        }
        var hasDate = o.year || o.month || o.day || o.weekday || o.dateStyle;
        var hasTime = o.hour || o.minute || o.second || o.timeStyle;
        if (!hasDate && !hasTime) {
            hasDate = true;
        }
        var out = [];
        if (hasDate) {
            var parts = [];
            if (o.weekday) {
                parts.push(o.weekday === "long" ? dayNames[d.getDay()] : dayNames[d.getDay()].slice(0, 3));
            }
            if (o.month === "long" || o.month === "short" || o.dateStyle === "long" || o.dateStyle === "full" || o.dateStyle === "medium") {
                var m = monthNames[d.getMonth()];
                parts.push((o.month === "short" || o.dateStyle === "medium" ? m.slice(0, 3) : m) + " " + d.getDate() + ", " + d.getFullYear());
            } else {
                parts.push((d.getMonth() + 1) + "/" + d.getDate() + "/" + d.getFullYear());
            }
            out.push(parts.join(", "));
        }
        if (hasTime) {
            var h = d.getHours();
            var hour12 = o.hour12 !== false && o.hourCycle !== "h23";
            var t = (hour12 ? (h % 12 || 12) : String(h).padStart(2, "0")) + ":" + String(d.getMinutes()).padStart(2, "0");
            if (o.second || o.timeStyle === "medium" || o.timeStyle === "long") {
                t += ":" + String(d.getSeconds()).padStart(2, "0");
            }
            out.push(hour12 ? t + (h < 12 ? " AM" : " PM") : t);
        }
        return out.join(", ");
    });
    define(DateTimeFormat.prototype, "formatToParts", function (date) {
        return [{ type: "literal", value: this.format(date) }];
    });
    define(DateTimeFormat.prototype, "resolvedOptions", function () {
        return Object.assign({ locale: intlLocale(), calendar: "gregory", numberingSystem: "latn", timeZone: "UTC" }, this._options);
    });
    function Collator(locale, options) {
        if (!(this instanceof Collator)) {
            return new Collator(locale, options);
        }
        var o = options || {};
        this.compare = function (a, b) {
            a = String(a);
            b = String(b);
            if (o.sensitivity === "base" || o.sensitivity === "accent") {
                a = a.toLowerCase();
                b = b.toLowerCase();
            }
            if (o.numeric) {
                return a.localeCompare(b, undefined, { numeric: true });
            }
            return a < b ? -1 : a > b ? 1 : 0;
        };
    }
    define(Collator.prototype, "resolvedOptions", function () {
        return { locale: intlLocale(), usage: "sort", sensitivity: "variant" };
    });
    function PluralRules(locale, options) {
        if (!(this instanceof PluralRules)) {
            throw new TypeError("Constructor PluralRules requires 'new'");
        }
        this._ordinal = !!(options && options.type === "ordinal");
    }
    define(PluralRules.prototype, "select", function (n) {
        n = Number(n);
        if (this._ordinal) {
            var m10 = n % 10, m100 = n % 100;
            return m10 === 1 && m100 !== 11 ? "one" : m10 === 2 && m100 !== 12 ? "two" : m10 === 3 && m100 !== 13 ? "few" : "other";
        }
        return n === 1 ? "one" : "other";
    });
    function RelativeTimeFormat(locale, options) {
        if (!(this instanceof RelativeTimeFormat)) {
            throw new TypeError("Constructor RelativeTimeFormat requires 'new'");
        }
        this._numeric = options && options.numeric;
    }
    define(RelativeTimeFormat.prototype, "format", function (value, unit) {
        value = Number(value);
        unit = String(unit).replace(/s$/, "");
        var abs = Math.abs(value);
        var label = abs === 1 ? unit : unit + "s";
        return value < 0 || Object.is(value, -0) ? abs + " " + label + " ago" : "in " + abs + " " + label;
    });
    if (typeof global.Intl === "undefined") {
        var intl = {};
        define(intl, "NumberFormat", NumberFormat);
        define(intl, "DateTimeFormat", DateTimeFormat);
        define(intl, "Collator", Collator);
        define(intl, "PluralRules", PluralRules);
        define(intl, "RelativeTimeFormat", RelativeTimeFormat);
        define(intl, "getCanonicalLocales", function (l) {
            return l === undefined ? [] : [].concat(l).map(String);
        });
        define(intl, Symbol.toStringTag, "Intl");
        define(global, "Intl", intl);
    }

    if (typeof console.table !== "function") {
        define(console, "table", function (data) {
            console.log(data);
        });
    }

    var XHR_UNSENT = 0, XHR_OPENED = 1, XHR_HEADERS = 2, XHR_LOADING = 3, XHR_DONE = 4;
    function XMLHttpRequest() {
        if (!new.target) {
            throw new TypeError("Constructor XMLHttpRequest requires 'new'");
        }
        define(this, "_state", XHR_UNSENT);
        define(this, "_method", "GET");
        define(this, "_url", null);
        define(this, "_headers", new Headers());
        define(this, "_response", null);
        define(this, "_controller", null);
        define(this, "_sent", false);
        define(this, "_timer", null);
        define(this, "_mime", null);
        this.responseType = "";
        this.timeout = 0;
        this.withCredentials = false;
        this.upload = Object.create(SimpleEventTarget.prototype);
        ["onreadystatechange", "onload", "onerror", "onloadend", "onloadstart", "onprogress", "onabort", "ontimeout"].forEach(function (n) {
            this[n] = null;
        }, this);
    }
    function XMLHttpRequestEventTarget() {
        throw new TypeError("Illegal constructor");
    }
    Object.setPrototypeOf(XMLHttpRequestEventTarget.prototype, SimpleEventTarget.prototype);
    define(XMLHttpRequestEventTarget.prototype, Symbol.toStringTag, "XMLHttpRequestEventTarget");
    defineMissing(global, "XMLHttpRequestEventTarget", XMLHttpRequestEventTarget);
    Object.setPrototypeOf(XMLHttpRequest.prototype, XMLHttpRequestEventTarget.prototype);
    [["UNSENT", XHR_UNSENT], ["OPENED", XHR_OPENED], ["HEADERS_RECEIVED", XHR_HEADERS], ["LOADING", XHR_LOADING], ["DONE", XHR_DONE]].forEach(function (c) {
        Object.defineProperty(XMLHttpRequest, c[0], { value: c[1], enumerable: true });
        Object.defineProperty(XMLHttpRequest.prototype, c[0], { value: c[1], enumerable: true });
    });
    function xhrSetState(xhr, state) {
        xhr._state = state;
        xhr._emit("readystatechange");
    }
    Object.defineProperty(XMLHttpRequest.prototype, "readyState", { get: function () { return this._state; }, configurable: true });
    Object.defineProperty(XMLHttpRequest.prototype, "status", { get: function () { return this._response ? this._response.status : 0; }, configurable: true });
    Object.defineProperty(XMLHttpRequest.prototype, "statusText", { get: function () { return this._response ? this._response.statusText : ""; }, configurable: true });
    Object.defineProperty(XMLHttpRequest.prototype, "responseURL", { get: function () { return this._response ? this._response.url : ""; }, configurable: true });
    Object.defineProperty(XMLHttpRequest.prototype, "responseText", {
        get: function () {
            if (this.responseType !== "" && this.responseType !== "text") {
                throw domException("responseText is only available if responseType is '' or 'text'.", "InvalidStateError");
            }
            return this._state < XHR_LOADING || !this._response ? "" : this._text;
        },
        configurable: true
    });
    Object.defineProperty(XMLHttpRequest.prototype, "responseXML", { get: function () { return null; }, configurable: true });
    Object.defineProperty(XMLHttpRequest.prototype, "response", {
        get: function () {
            if (this.responseType === "" || this.responseType === "text") {
                return this.responseText;
            }
            if (this._state !== XHR_DONE || !this._response) {
                return null;
            }
            switch (this.responseType) {
            case "json":
                try {
                    return JSON.parse(this._text);
                } catch (e) {
                    return null;
                }
            case "arraybuffer":
                return this._buffer.slice(0);
            case "blob":
                return new Blob([this._buffer], { type: this._response.headers.get("content-type") || "" });
            default:
                return null;
            }
        },
        configurable: true
    });
    define(XMLHttpRequest.prototype, "open", function (method, url, async) {
        if (async === false) {
            throw domException("Synchronous requests are not supported.", "InvalidAccessError");
        }
        var resolved = native.resolve(String(url));
        if (resolved === null) {
            throw domException("Invalid URL", "SyntaxError");
        }
        if (this._controller) {
            this._controller.abort();
        }
        this._method = String(method).toUpperCase();
        this._url = resolved;
        this._headers = new Headers();
        this._response = null;
        this._sent = false;
        xhrSetState(this, XHR_OPENED);
    });
    define(XMLHttpRequest.prototype, "setRequestHeader", function (name, value) {
        if (this._state !== XHR_OPENED || this._sent) {
            throw domException("The object's state must be OPENED.", "InvalidStateError");
        }
        this._headers.append(name, value);
    });
    define(XMLHttpRequest.prototype, "overrideMimeType", function (mime) {
        this._mime = String(mime);
    });
    define(XMLHttpRequest.prototype, "getResponseHeader", function (name) {
        return this._response && this._state >= XHR_HEADERS ? this._response.headers.get(name) : null;
    });
    define(XMLHttpRequest.prototype, "getAllResponseHeaders", function () {
        if (!this._response || this._state < XHR_HEADERS) {
            return "";
        }
        var out = "";
        this._response.headers.forEach(function (value, name) {
            out += name + ": " + value + "\r\n";
        });
        return out;
    });
    define(XMLHttpRequest.prototype, "send", function (body) {
        var xhr = this;
        if (this._state !== XHR_OPENED || this._sent) {
            throw domException("The object's state must be OPENED.", "InvalidStateError");
        }
        this._sent = true;
        var controller = new AbortController();
        this._controller = controller;
        var init = { method: this._method, headers: this._headers, signal: controller.signal, credentials: this.withCredentials ? "include" : "same-origin" };
        if (this._method !== "GET" && this._method !== "HEAD" && body !== undefined && body !== null) {
            init.body = body;
        }
        this._emit("loadstart");
        if (this.timeout > 0) {
            this._timer = global.setTimeout(function () {
                if (xhr._controller === controller) {
                    xhr._controller = null;
                    controller.abort();
                    xhrSetState(xhr, XHR_DONE);
                    xhr._emit("timeout");
                    xhr._emit("loadend");
                }
            }, this.timeout);
        }
        fetch(this._url, init).then(function (response) {
            if (xhr._controller !== controller) {
                return;
            }
            xhr._response = response;
            xhrSetState(xhr, XHR_HEADERS);
            return response.arrayBuffer().then(function (buffer) {
                if (xhr._controller !== controller) {
                    return;
                }
                xhr._controller = null;
                if (xhr._timer !== null) {
                    global.clearTimeout(xhr._timer);
                    xhr._timer = null;
                }
                xhr._buffer = buffer;
                xhr._text = native.utf8Decode(buffer);
                xhrSetState(xhr, XHR_LOADING);
                xhr._emit("progress", { loaded: buffer.byteLength, total: buffer.byteLength, lengthComputable: true });
                xhrSetState(xhr, XHR_DONE);
                xhr._emit("load", { loaded: buffer.byteLength, total: buffer.byteLength, lengthComputable: true });
                xhr._emit("loadend");
            });
        }).catch(function (error) {
            if (xhr._controller !== controller) {
                return;
            }
            xhr._controller = null;
            if (xhr._timer !== null) {
                global.clearTimeout(xhr._timer);
                xhr._timer = null;
            }
            xhr._response = null;
            xhrSetState(xhr, XHR_DONE);
            xhr._emit("error");
            xhr._emit("loadend");
        });
    });
    define(XMLHttpRequest.prototype, "abort", function () {
        if (this._controller) {
            var controller = this._controller;
            this._controller = null;
            controller.abort();
            this._response = null;
            if (this._state !== XHR_UNSENT) {
                xhrSetState(this, XHR_DONE);
                this._emit("abort");
                this._emit("loadend");
            }
        }
        this._state = XHR_UNSENT;
    });
    define(XMLHttpRequest.prototype, Symbol.toStringTag, "XMLHttpRequest");
    defineMissing(global, "XMLHttpRequest", XMLHttpRequest);

    var computedDefaults = {
        "transform": "none", "pointer-events": "auto", "box-sizing": "content-box",
        "top": "auto", "right": "auto", "bottom": "auto", "left": "auto",
        "min-width": "0px", "min-height": "0px", "max-width": "none", "max-height": "none",
        "text-align": "start", "white-space": "normal", "clear": "none", "cursor": "auto",
        "transition-duration": "0s", "animation-name": "none", "content": "normal"
    };

    function hyphenate(name) {
        return name.replace(/[A-Z]/g, function (c) { return "-" + c.toLowerCase(); });
    }

    return {
        flushReactions: flushReactions,
        noteTemplate: noteTemplate,
        makeSlot: function (el) {
            var proto = Object.getPrototypeOf(el);
            if (proto === HTMLElement.prototype ||
                (global.HTMLUnknownElement && proto === global.HTMLUnknownElement.prototype)) {
                Object.setPrototypeOf(el, HTMLSlotElement.prototype);
            }
        },
        moveTemplates: moveTemplates,
        resolveModule: resolveModule,
        moduleText: function (url) {
            return moduleTexts.get(url);
        },
        loadModuleGraph: function (url) {
            return loadModuleGraph(url);
        },
        loadModuleScript: loadModuleScript,
        runModules: runModules,
        addImportMap: addImportMap,
        makeComputedStyle: function (values) {
            var names = Object.keys(computedDefaults).concat(Object.keys(values)).filter(function (n, i, a) {
                return a.indexOf(n) === i;
            });
            var decl = Object.create(null);
            names.forEach(function (n) {
                decl[n] = n in values ? values[n] : computedDefaults[n];
            });
            var target = {
                getPropertyValue: function (name) {
                    name = String(name).toLowerCase();
                    return name in decl ? decl[name] : "";
                },
                getPropertyPriority: function () { return ""; },
                item: function (i) { return names[i] || ""; },
                setProperty: function () {
                    throw new DOMException("The computed style is read-only", "NoModificationAllowedError");
                },
                removeProperty: function () {
                    throw new DOMException("The computed style is read-only", "NoModificationAllowedError");
                },
                length: names.length,
                cssText: "",
                parentRule: null
            };
            target[Symbol.toStringTag] = "CSSStyleDeclaration";
            return new Proxy(target, {
                get: function (t, key) {
                    if (typeof key === "string") {
                        if (key in t) {
                            return t[key];
                        }
                        var index = listIndex(key);
                        if (index >= 0) {
                            return names[index];
                        }
                        var h = key.indexOf("-") >= 0 ? key : hyphenate(key);
                        if (h in decl) {
                            return decl[h];
                        }
                    }
                    return t[key];
                },
                has: function (t, key) {
                    return key in t || (typeof key === "string" && (key in decl || hyphenate(key) in decl));
                },
                set: function () {
                    return true;
                }
            });
        },
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
                    if (typeof key === "string" && key !== "" && !(key in target) &&
                        typeof target.namedItem === "function") {
                        var named = target.namedItem(key);
                        return named === null ? undefined : named;
                    }
                    return target[key];
                }
            });
        },
        makeFormProxy: function (inner) {
            var proxy;
            function named(target, key) {
                if (typeof key !== "string" || key === "" || key in target) {
                    return undefined;
                }
                var elements = Reflect.get(target, "elements", proxy);
                var index = listIndex(key);
                if (index >= 0) {
                    return elements[index];
                }
                return elements.namedItem(key) || undefined;
            }
            proxy = new Proxy(inner, {
                has: function (target, key) {
                    return key in target || named(target, key) !== undefined;
                },
                get: function (target, key, receiver) {
                    var n = named(target, key);
                    return n !== undefined ? n : Reflect.get(target, key, receiver);
                },
                set: function (target, key, value, receiver) {
                    return Reflect.set(target, key, value, receiver);
                }
            });
            return proxy;
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
