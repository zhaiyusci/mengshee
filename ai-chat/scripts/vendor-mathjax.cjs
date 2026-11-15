// Copies MathJax's single-file TeX→SVG build next to the page so the window
// works offline: this application must not depend on a CDN.
const fs = require("node:fs");
const path = require("node:path");

const source = path.join(__dirname, "..", "node_modules", "mathjax", "es5", "tex-svg.js");
const targetDirectory = path.join(__dirname, "..", "src", "vendor");
const target = path.join(targetDirectory, "tex-svg.js");

if (!fs.existsSync(source)) {
    console.error("MathJax is not installed. Run npm install first.");
    process.exit(1);
}
fs.mkdirSync(targetDirectory, { recursive: true });
fs.copyFileSync(source, target);
console.log("MathJax vendored to", target);
