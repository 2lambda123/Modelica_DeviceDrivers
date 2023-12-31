within Modelica_DeviceDrivers.EmbeddedTargets.AVR.Blocks;
block Microcontroller "Use as an inner block, defining the characteristics of the AVR microcontroller"
  import SIunits =
         Modelica.Units.SI;
  import Modelica_DeviceDrivers;
  import Modelica_DeviceDrivers.EmbeddedTargets.AVR.Constants;
  import Modelica_DeviceDrivers.EmbeddedTargets.AVR.Types;
  extends Modelica_DeviceDrivers.Utilities.Icons.GenericIC;
  constant Types.Platform platform annotation(Dialog(
    enable = true,
    tab = "General",
    group = "Constants"));
  constant SIunits.Frequency cpuFrequency = Constants.cpuFrequency[platform] "Default frequency is the platform default (can be modified)" annotation(Dialog(
    enable = true,
    tab = "General",
    group = "Constants"));
  constant SIunits.Frequency minADCFrequency = Constants.minADCFrequency[platform] "Minimum recommended frequency to sample the ADC" annotation(Dialog(
    enable = true,
    tab = "General",
    group = "Constants"));
  constant SIunits.Frequency maxADCFrequency = Constants.maxADCFrequency[platform] "Maximum recommended frequency to sample the ADC" annotation(Dialog(
    enable = true,
    tab = "General",
    group = "Constants"));
  constant Integer adcResolution = Constants.adcResolution[platform] "Bits of resolution in the ADC" annotation(Dialog(
    enable = true,
    tab = "General",
    group = "Constants"));
  constant SIunits.Frequency desiredFrequency=if desiredPeriod==0 then 0 else (1/desiredPeriod) "Used to try to select a reasonable default pre-scaler and counter for real-time synchronization. Select this or a period (automatically re-calculated)" annotation(Dialog(
    enable = true,
    tab = "Real-time",
    group = "Constants"));
  constant SIunits.Time desiredPeriod=0 "Used to try to select a reasonable default pre-scaler and counter for real-time synchronization." annotation(Dialog(
    enable = true,
    tab = "Real-time",
    group = "Constants"));
  annotation(missingInnerMessage = "Missing inner block for AVR microcontroller (this cannot have default values since the microcontrollers are all different).",
             defaultComponentName="mcu",
             defaultComponentPrefixes="inner",
             Icon(graphics={  Text(textColor = {255, 255, 255}, extent = {{-50, -50}, {50, 50}}, textString = "AVR\n%platform", fontName = "Arial", textStyle = {TextStyle.Bold})}, coordinateSystem(initialScale = 0.1)));
end Microcontroller;
