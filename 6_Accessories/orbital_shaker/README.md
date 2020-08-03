
# CitizenSensor Orbital Shaker

![CitizenSensor Orbital Shaker](https://github.com/CitizenSensor/CitizenSensor/blob/master/Wiki/images/CS_OrbitalShaker.gif?raw=true)

The CitizenSensor Orbital Shaker is an Open Source device for shaking fluids.
This device was designed for for step ["Creation of Aqueous Solution"](https://github.com/CitizenSensor/CitizenSensor/blob/master/Wiki/CS_Usage_AqueousSolution.md), where an aqueous solution
is created by dissolving a soil sample in water. 

The design of the CitizenSensor Orbital Shaker has the following objectives:
- Construction based on few and simple components
- Support of sufficient load capacity (filled jam jar with a total weight of 450g)
- Robust mechanics for mixing the aqueous solution over a time period of 2 hours

## Assembly Instructions

1. Print the 3D Parts P1 - P3 (Recommended print parameters: 3 Shells, Infill 90%)
2. Sand the inner side of the circular ring of part P2 to reduce friction of the rotating nail
3. Place the stepper motor on a wood panel (at least 10 cm x 10 cm) and mark the locations of the holes in the stepper motor.
4. Drill the holes for fastening the stepper motor. Us a drill with a diameter that matches the screws for the stepper motor S1.
5. Place the Orbital Shaker Base P1 on the wood panel and put the Stepper Motor M1 on top.
6. Fasten the stepper motor M1 with the four screws S1.
7. Screw the clamping ring R1 tight on the stepper motor axis
8. Drill a little hole into the Orbital Shaker Rotating Disc - just as big that the thin nail N1 has a tight fits 
9. Mount the Orbital Shaker Rotating Disc on the clamping ring R1
10. Twist the six thin tubes in the holes of the shaker base 
11. Mount the Orbital Shaker Top: twist in the six thin tubes in the holes of the shaker top
12. Place the Orbital Shaker Platform P4 on the Orbital Shaker Top P2.
13. Connect the stepper motor to the stepper motor driver
14. Connect the stepper motor to the 12V power source
15. Connect the Arduino to the 5V power source
The Orbital Shaker Rotating Disc should now move the Orbital Shaker Top.
16. Disconnect the Arduino from the power source
17. Put the jam jar on the Orbital Shaker Platform, then reconnect the Arduino to the power source. The Orbital Shaker should now shake the jam jar.

In case the mechanics does not properly move, re-check that all tubes properly fit in the Orbital Shaker Base and Top. Also the Orbital Shaker Top might need smoe more sanding. 


## Bill of Material

| Item | Description | Amount | Comment  |
| --- | --- | ---| ---  |
| P1 | Orbital Shaker Base | 1 | 3D Print |
| P2 | Orbital Shaker Top | 1 | 3D Print |
| P3 | Orbital Shaker Rotating Disc | 1 | 3D Print |
| P4 | Orbital Shaker Platform | 1 | Lasercut |
| M1 | Stepper Motor | 1 | |
| R1 | Clamping Ring | 1 | Inside diameter must correspond to stepper motor axis |
| S1-S4 | Screws for Stepper Motor | 4 | |
| A1 | Arduino | 1 | |
| C1 | Stepper Motor Driver | 1 | |
| PS1 | Power Supply 12V | 1 | |
| PS2 | Power Supply 5V | 1 | |
| C1 | USB Cable | 1 | |
| T1-T6 | Thin Tube | 6 | Length: 82 mm, Outside Diameter 5mm |
| N1 | Thin Nail | 1 | |


## Credits

The CitizenSensor Orbital Shaker was inspired by the following projects:

* [Hackteria Orbital Shaker](https://www.hackteria.org/wiki/Orbital_Shaker)
* [Open Source Orbital Shaker by jmil (Thingiverse)](https://www.thingiverse.com/thing:5045)
* [DIYbio Orbital Shaker V 1.0 by ProgressTH (Thingiverse)](https://www.thingiverse.com/thing:2633507)
* [Crickit Lab Shaker by Ruiz Brothers (Adafruit)](https://learn.adafruit.com/crickit-lab-shaker/)

## License

Copyright FabLab Muenchen e.V. and Fraunhofer EMFT 2019.

This documentation describes Open Hardware and is licensed under the CERN OHL v. 1.2.

You may redistribute and modify this documentation under the terms of the CERN OHL v.1.2. 
(http://ohwr.org/cernohl). This documentation is distributed WITHOUT ANY EXPRESS OR 
IMPLIED WARRANTY, INCLUDING OF MERCHANTABILITY, SATISFACTORY QUALITY AND FITNESS FOR A 
PARTICULAR PURPOSE. 

Please see the CERN OHL v.1.2 for applicable conditions.