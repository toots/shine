Shine.initialized = new Promise(function (resolve) {
  Module['onRuntimeInitialized'] = function () {
    ShineModule = Module;
    shineInit();
    resolve();
  }
})

Module["Shine"] = Shine;
Module["StereoMode"] = StereoMode;
