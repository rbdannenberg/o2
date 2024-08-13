// dprint.js -- console-like text output to webpage
// Roger B. Dannenberg, November 2015
// 
// DOCUMENTATION:
//
// dPrint(a, b, c, ...) -- convert arguments to strings and 
//     print them with one space separation
// dPrintln() is identical to dprint() except that it prints
//     a newline after printing the arguments
// dLines(n) -- restrict the number of lines to appear,
//     if n is negative, there is no restriction
// dPrecision(d) -- prints floating point numbers with up to d places
//     to the right of the decimal point. If d is negative, numbers are
//     printed using the default toString() method.
// dColor(color) -- set text color for future prints
// createDlines(width, height) -- specify output size in pixels. This
//     is not required, but if called, output will appear in a scrollable box
// removeDlines() -- removes output, but output may be resumed. To resume
//     output to a scrollable box, call createDlines() again.


// EXAMPLES:
//
// dPrintln("Hello World") -- prints "Hello World\n"
// dPrint(3, 2, 1, 'go') -- prints "3 2 1 go"
// dPrintln("" + 3 + 2 + 1 + 'go') -- prints "321go"
// dPrecision(3); dPrint(12.0, 1.23456) -- prints "12 1.234"

var dpLinesArray = []; // an array of <div> elements
var dLastLineEnded = false;
var dLinesMax = -1; // means no limit
var dDigits = -1; // means no limit
var dpLinesBox;
var dpColorValue = false;


function createDlines(w, h) {
    removeDlines(); // don't leave stragglers
    dpLinesBox = document.createElement("DIV");
    dpLinesBox.style.fontFamily = "monospace";
    dpLinesBox.style.width = "" + w + "px";
    dpLinesBox.style.height = "" + h + "px";
    dpLinesBox.style.border = "1px solid #ccc";
    dpLinesBox.style.overflow = "auto";
    dpLinesBox.style.wordWrap = "break-word";
    var dp = document.getElementById("dPrint")
    if (!dp) {
        dp = document.body;
    }
    dp.appendChild(dpLinesBox);
}


function removeDlines() {
    if (dpLinesBox) {
        dpLinesBox.remove();
        dpLinesBox = false;
    } else {
        for (var i = 0; i < dpLinesArray.length; i++) {
            dpLinesArray[i].remove();
        }
    }
    dLastLineEnded = false;
    dpLinesArray = [];
}


function dLines(n) {
    dLinesMax = n;
}


function dPrecision(n) {
    dDigits = n;
}


function dColor(color) {
    dpColorValue = color;
}


function dPrint() {
    dpPrintValues(arguments);
}


function dPrintLn() {
    dpPrintValues(arguments);
    dpNewLine();
}


function dpPrintValues(args) {
    var s = "";
    for (var i = 0; i < args.length; i++) {
        var x = args[i];
        if (typeof x === "number" && dDigits >= 0) {
            // convert to string with dDigits, then remove trailing 0's and .:
            x = x.toFixed(dDigits).replace(/\.?0*$/,'')
        } else {
            x = x.toString();
        }
        s = s + x
        if (i < args.length - 1) s = s + " ";
    }
    while (s.length > 0) {
        var newlineIndex = s.indexOf("\n");
        if (newlineIndex != -1) {
            var sub = s.substring(0, newlineIndex);
            dpPrintString(sub);
            dpNewLine();
            s = s.substring(newlineIndex + 1);
        } else {
            dpPrintString(s);
            s = "";
        }
    }
}


function dpNewLine() {
    dpNeedAtLeastOneLine();
    dpAppendEmptyLine();
}


function dpAppendEmptyLine() {
    if (dpLinesBox) {
        dpLinesArray.push("");
    } else {
        var div = document.createElement("DIV");
        div.style.fontFamily = "monospace";
        div.style.wordWrap = "break-word";
        dpLinesArray.push(div);
    }
}


function dpNeedAtLeastOneLine() {
    if (dpLinesArray.length === 0) {
        dpAppendEmptyLine();
    }
}


function dpEscape(s) {
    return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').
             replace(/>/g, '&gt;').replace(/ /g, '&nbsp;');
}


function dpPrintString(s) {
    s = dpEscape(s);
    if (dpColorValue) {
        s = '<span style="color: ' + dpColorValue + ';">' + s + '</span>';
    }
    dpNeedAtLeastOneLine();
    if (dpLinesBox) { // dpLinesArray has strings to put in box
        dpLinesArray[dpLinesArray.length - 1] += s;
        if (dLinesMax >= 0) {
            dpLinesArray = dpLinesArray.slice(-dLinesMax);
        }
        var allText = "";
        for (var i = 0; i < dpLinesArray.length; i++) {
            allText += dpLinesArray[i];
            if (i < dpLinesArray.length - 1) {
                allText += "<br\>";
            }
        }
        dpLinesBox.innerHTML = allText;
    } else { // dpLinesArray has divs
        var html = dpLinesArray[dpLinesArray.length - 1].innerHTML;
        dpLinesArray[dpLinesArray.length - 1].innerHTML = html + s;
        if (dLinesMax >= 0 && dpLinesArray.length > dLinesMax) {
            for (var i = 0; i < dpLinesArray.length - dLinesMax; i++) {
                dpLinesArray[i].remove();
            }
            dpLinesArray = dpLinesArray.slice(-dLinesMax);
        }
    }
}
