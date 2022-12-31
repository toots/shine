import { Shine } from "@toots/shine.js";
import runRoundsTest from "../lib/rounds";

const exec = async () => {
  await Shine.initialized;

  console.log("");
  runRoundsTest(Shine, s => console.log(s));
};

exec();
