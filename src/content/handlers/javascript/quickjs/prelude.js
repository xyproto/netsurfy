(function () {
    "use strict";

    var indexPattern = /^(0|[1-9][0-9]*)$/;

    function listIndex(key) {
        if (typeof key === "string" && indexPattern.test(key)) {
            return Number(key);
        }
        return -1;
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
})()
