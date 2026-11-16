(function () {
    function report(line) {
        console.log("TEST " + line);
    }
    function describe(value) {
        try {
            return JSON.stringify(value);
        } catch (e) {
            return String(value);
        }
    }
    window.test = function (name, fn) {
        try {
            fn();
            report("PASS " + name);
        } catch (e) {
            report("FAIL " + name + ": " + (e && e.message ? e.message : e));
        }
    };
    window.assert = function (condition, message) {
        if (!condition) {
            throw new Error(message || "assertion failed");
        }
    };
    window.assertEquals = function (actual, expected, message) {
        if (actual !== expected) {
            throw new Error((message ? message + ": " : "") + "expected " + describe(expected) + " but got " + describe(actual));
        }
    };
    window.assertThrows = function (name, fn) {
        try {
            fn();
        } catch (e) {
            if (e && e.name === name) {
                return;
            }
            throw new Error("expected " + name + " but got " + (e && e.name));
        }
        throw new Error("expected " + name + " to be thrown");
    };
    window.done = function () {
        report("DONE");
    };
})();
