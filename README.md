Polar Control
=============

Polar Control
-------------
ramp_dist: Ramp generator for distance set points
ramp_speed: Ramp generator for speed set points

odo: Dead reckoning
asserv: Speed/value control loops, you usually instanciate it twice: one for delta, the other for alpha.
pid: PID, 2 instances are used for each asserv object (one for the value asserv, the other for the speed asserv)
motion: Controls the asserv module, taking into account ramp_dist and ramp_speed. Provides the following important functions:
- motion_dist_rot, control of distance and/or rotation (relative)
- motion_speed, control of speeds for linear trajectories
- motion_omega, control of rotational speed
- motion_speed_omega, control of both linear speed and rotational speed
The other motion functions could cause jerks / sharp stops if they are chained clumsily.
Currently, this is also true for linear speed control and omega. Take care of having stopped relative to the other axis.
The end of control is not handled perfectly yet : only the distance and rotation control will signal when reaching their target / the end of the ramp. Code should be added to read the PID value at some point.


Simu
----
Simulator with python + box2d


Pic
---
Code for dsPIC33F
