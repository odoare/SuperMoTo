- Calibration : Add comments text entriers that are saved in the xml/markdown file. Two text entries:

        * a general comment for the series of measurements (accessible via a button: you click and open a text entry). If modified between two series of measurements, it it written again at the next file saved
        * a comment for one series of measurements : a one line text entry.

- Calibration : If the microphone has been calibrated using an SPL meter, write the calibration coefficient in the files

- Calibration  : If a microphone correction curve has been used, write the data in the file too

- Analysis group analysis : If a calibration data is found in the file, use it. It is still possible to load a different calibration file. In the case of single analysis tab, we should look for an xml info file in the folder to look for that information.

- Analysis and group analysis : Currently the plotted frequency response data is normalized to a 0 dB reference. That could be interesting to have an option with no normalization. If it was done on a calibrated data, we could even see the actual SPL.

- Analysis and group analysis : Plot impulse response (switch from frequency response to impulse response using a dropdown)

- Analysis and group analysis : If the log sweep has been used for the measurement, we should implement the Farina's method for system's identification (linear+nonlinear components+phase). Please refer to Novak et al, JAES 2015 "Synchronized swept-sine: Theory, application, and implementation" for the implementation. Do it again by putting reusable code in FxmeTools. Here again, if we are in the single analysis tab, we look for the xml file in the folder to get the information on the used forcing signal.

In order to plot the impulse response, we should implement a new component that can display audio data in the time domain. The component will be used in other projects. We should hence implement it in FxmeTools. It should be capable of drawing short to long signals (dozens of seconds), be used for realtime monitoring of signals, as well as drawing static buffers, or wavefile data. It should have a marker/region system. Zooming in/out should be implemented: Mouse wheel for vertical zooming. Ctl-mouse for horizontal zooming, and click dra for navigating in the data. Double-click should rested to the full view. Axis label + grid should be drawn the same way as the spectrum plots. Please implement it with a clear and reusable API, and properly document the header.

In the spectrum and phase display components, we should also implement the ctl-wheel to zoom horizontally, click-drag in both directions, and double-click for default view.

Reorganize eventually the controls for better readability/logic. The order of controls should reflect the order of actions. For instance, in the group analysis, the load measurement folder should come first. See everywhere if something could be done to improve the GUI look and feel.
