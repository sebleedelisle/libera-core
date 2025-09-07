#pragma once 

#include "Controller.hpp"

// Ether Dream controller. 
// To figure out! 
// 1.   Connect to DAC
//      Requires networking code for opening and connecting to a 
//      TCP socket. Along with all the error checking that goes with
//      that. 
// 2.   Sending points.
//      Convert from laser points to ether dream points.
//      Create command packets
// 3.   Main thread logic
//      When to ask for more points. When to send them 
// 4.   Receiving ACKs and parse 
// 5.   Status information
//      What is universal status info vs what is ether dream specific? 
// TODO 
// Start with a minimal example
// Connects to network. 
// Starts thread
// Stops on stop command

// Thread logic : 



namespace libera::core {
class EtherDreamController : public Controller {




};
}