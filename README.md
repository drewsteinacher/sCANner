# sCANner
An Arduino CAN bus scanner/sniffer.

### Overview
This started as a project to see what data was available on the CAN bus in my car.
I quickly realized that I needed to improve its performance to capture as many messages as possible.
Additionally, I found that the mass amounts of data these sketches produced required an interactive system to more easily perform explorations, and create visualizations.
My solution to this problem was to create a GUI written in the Wolfram Language.

### Dependencies
These sketches rely on the following CAN library:
https://github.com/coryjfowler/MCP_CAN_lib

### Hardware
I use an Arduino Uno and the [CAN-BUS shield V1.2 from Seeed](http://wiki.seeed.cc/CAN-BUS_Shield_V1.2/), but other Arduinos and CAN shields based on the MCP2515 should also work with some modifications.
