var createModule = require("./libshine_browser_stubs").default;

Shine.initialized = (createModule()).then(function (Module) {
  ShineModule = Module;
  shineInit();
});

module.exports.Shine = Shine;

module.exports.StereoMode = StereoMode;
