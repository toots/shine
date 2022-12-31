Shine.initialized = new Promise(function (resolve) {
  Module['onRuntimeInitialized'] = function () {
    ShineModule = Module;
    shineInit();
    resolve();
  }
})

Module["Shine"] = Shine;

Module["StereoMode"] = {
  '0': 'STEREO',
  '1': 'JOINT_STEREO',
  '2': 'DUAL_CHANNEL',
  '3': 'MONO',
  STEREO: 0,
  JOINT_STEREO: 1,
  DUAL_CHANNEL: 2,
  MONO: 3
};
