/*
 * nodeCreator.cc
 *
 *  Created on: Oct 29, 2015
 *      Author: linus
 */


#include "ns3/node.h"
#include "ns3/node-container.h"

using namespace ns3;
using namespace std;

#include <iostream>

class NodeCreator {

public:

	NodeCreator();

	void createNodes();

	int randomTime();

	void SetNode(Ptr <Node> node);
	void SetNodeContainer(NodeContainer nodesMesh);
	Ptr <Node> GetPtrNode();
	NodeContainer GetNodeContainer();



private:

	int nrNodes;
	int nodeTimer;

	Ptr <Node> m_node;
	NodeContainer m_nodes;


};

NodeCreator::NodeCreator() :
	nrNodes(0), nodeTimer(0)
{

}

void
NodeCreator::createNodes(){

	//for (int i = 0; i < n; i++) {

		//nodeTimer = randomTime();
		//m_nodes.Create(1, nodeTimer);
	//}

	cout << m_nodes.Get(0)->GetNodeTime() << endl;
	cout << m_nodes.Get(1)->GetNodeTime() << endl;


}

int
NodeCreator::randomTime(){

	int randomValue;
	randomValue = rand() % 100 + 1;
	return randomValue;

}
void
NodeCreator::SetNode(Ptr <Node> node)
{
	m_node = node;
}

void
NodeCreator::SetNodeContainer(NodeContainer nodes) {

	m_nodes = nodes;
}

Ptr <Node>
NodeCreator::GetPtrNode() {

	return m_node;
}

NodeContainer
NodeCreator::GetNodeContainer() {

	return m_nodes;
}

