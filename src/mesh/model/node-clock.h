/*
 * node-clock.h
 *  Created on: Apr 29, 2015
 *      Author: linus
 */

#ifndef SRC_MESH_MODEL_NODE_CLOCK_H_
#define SRC_MESH_MODEL_NODE_CLOCK_H_

#include "ns3/core-module.h"
#include <ctime>
#include <iostream>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include "ns3/object.h"

namespace ns3 {

class NodeClock
{
public:

	NodeClock();

	long nodeClockCalc();

	long randomTime();




  };
}

#endif /* SRC_MESH_MODEL_NODE_CLOCK_H_ */
