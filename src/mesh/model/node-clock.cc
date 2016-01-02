/*
 * node-clock.cc
 *
 *  Created on: Apr 29, 2015
 *      Author: linus and Tequamnesh
 */


#include "node-clock.h"
#include "ns3/nstime.h"
#include <sys/time.h>
#include <ctime>
#include <stdlib.h>

using namespace std;
using namespace ns3;


//Constructor
NodeClock::NodeClock()
	{

	}
// Method which return time of day in milliseconds
long
NodeClock::nodeClockCalc()
{

		timeval timeMilli;
		gettimeofday(&timeMilli, NULL);
		long millis = ((timeMilli.tv_sec % 86400) * 1000 + timeMilli.tv_usec / 1000) + 3600000;
		long total = millis + randomTime();

	 return total;

	}

// method which adds a random number between 1 -1000 to time of day in milliseconds
long
NodeClock::randomTime()
{

	int randomValue;
	randomValue = rand() % 1000 + 1;
	return randomValue;

}
