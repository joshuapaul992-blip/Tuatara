// Tuatara M1: SpiderMonkey shell checks for a real machine. Run with
// JSTEST.CMD (or "js.exe JSTEST.JS"). Prints one line per check, then a
// summary; the exit code is the number of failed checks.

var passed = 0, failed = 0;

function check(name, fn, expected) {
    var got;
    try {
        got = fn();
    } catch (e) {
        got = "threw " + e;
    }
    if (got === expected) {
        passed++;
        print("ok    " + name);
    } else {
        failed++;
        print("FAIL  " + name + ": got " + got + ", expected " + expected);
    }
}

check("arithmetic", function () { return 6 * 7; }, 42);
check("x87 double rounding", function () { return 0.1 + 0.2; }, 0.30000000000000004);
check("Math.sqrt", function () { return Math.sqrt(2); }, 1.4142135623730951);
check("Math.pow", function () { return Math.pow(2, 0.5) === Math.SQRT2; }, true);
check("number to string", function () { return (1 / 3).toString(); }, "0.3333333333333333");
check("parseFloat", function () { return parseFloat("1e-7") * 1e7; }, 1);
check("JSON", function () { return JSON.stringify({a: [1, 2.5, "x"], b: null}); },
      '{"a":[1,2.5,"x"],"b":null}');
check("RegExp", function () { return "2026-11-10".replace(/(\d+)-(\d+)-(\d+)/, "$3.$2.$1"); },
      "10.11.2026");
check("sort 100000 numbers", function () {
    var a = [];
    var seed = 1;
    for (var i = 0; i < 100000; i++) {
        seed = (seed * 1103515245 + 12345) % 2147483648;
        a.push(seed);
    }
    a.sort(function (x, y) { return x - y; });
    for (var j = 1; j < a.length; j++)
        if (a[j - 1] > a[j])
            return "unsorted at " + j;
    return a.length;
}, 100000);
check("string building (GC)", function () {
    var s = [];
    for (var i = 0; i < 200000; i++)
        s.push("item" + i);
    return s.join(",").length;
}, 2088889);
check("Date (UTC)", function () { return new Date(Date.UTC(2000, 0, 1)).toISOString(); },
      "2000-01-01T00:00:00.000Z");
check("Intl.NumberFormat (ICU)", function () {
    return new Intl.NumberFormat("en-US").format(1234567.891);
}, "1,234,567.891");
check("Intl.Collator (ICU)", function () {
    return ["b", "a", "C"].sort(new Intl.Collator("en").compare).join("");
}, "abC");
check("toLowerCase (ICU casing)", function () { return "İ".toLowerCase().length; }, 2);

print("");
print("passed " + passed + ", failed " + failed);
quit(failed);
