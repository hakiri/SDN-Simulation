/*
 * domain_controller.cc
 *
 *  Created on: 2019年2月4日
 *      Author: qinhongbo
 */




#include <stdio.h>
#include <string.h>
#include <omnetpp.h>
#include <string>
#include <condition.h>
#include <math.h>
#include <vector>
#include <algorithm>
#include "switch_message_m.h"

using namespace omnetpp;
using namespace std;

class domain_controller : public cSimpleModule
{
    public:
        double loss[20][20], transmissionDelay[20][20], queuingDelay[20][20];
        double availableBandwidth[20][20], totalBandwidth[20][20];
        int get; // the number of slave controller that has send network condition
        int src, des;
        int nex[20][20][20]; // forward table
        int num; // the number of the switches under its control
        bool isIn[20]; // if this switch is under domain controller's control
        bool G[20][20]; // connectivity condition

        // RL algorithm
        int visit[20][20];
        double Q[20][20][20][20], epsilon = 0.1, tau0 = 100, tauT = 0.05, T = 100;
        double beta1 = 1, beta2 = 1, beta3 = 1, theta1 = 1, phi1 = 1;
        double theta2 = 0.5, phi2 = 0.5;
        double alpha = 0.9, gamma = 0.7;
        double pi = cos(-1.0);

    protected:
      virtual void forwardMessageToSlave();
      virtual void initialize() override;
      virtual void handleMessage(cMessage *msg) override;
      virtual void copyCondition(condition* cond);
      virtual void retrieveCondition(cMessage* msg);
      virtual void getIsIn();
      virtual int makeSoftmaxPolicy(int state, double (& q)[20]);
      virtual void sarsa(double (& q)[20][20]);
      virtual double getTau();
      virtual double getReward();
};

Define_Module(domain_controller);


double domain_controller::getTau() {
    return -((tau0 - tauT) * visit[src][des]) / T + tau0;
}

struct Node {
    int idx;
    long double val;
    Node(int a = 0, long double b = 0) {
        idx = a;
        val = b;
    }
    bool cmp(Node a, Node b) {
        return a.val > b.val;
    }
};
int domain_controller::makeSoftmaxPolicy(int state, double (& q)[20]) {
    /*
     *  make epsilon greedy policy
     *
     *  Args:
     *      current statement
     *      map of "action -> reward"
     *
     *  Returns:
     *      the next hop
     */

    vector<Node> vec; // record the id of the surrounding nodes
    for (int i = 0; i < 20; ++i) if (isIn[i] && G[state][i])  vec.push_back(Node(i, q[i]));
    int len = vec.size();

    long double tauN = getTau();
    long double sum = 0;
    for (int i = 0; i < len; ++i) {
        vec[i].val = exp(vec[i].val / tauN);
        sum += vec[i].val;
    }

    for (int i = 0; i < len; ++i)
        vec[i].val /= sum;

    sort(vec.begin(), vec.end());
    long double chance = uniform(0, 1);
    for (int i = 0; i < len; ++i) {
        if (chance <= vec[i].val) return vec[i].idx;
        else chance -= vec[i].val;
    }
}


double domain_controller::getReward(int state, int nextState) {
    double totalQueuingDelay = 0;
    double totalTransmissionDelay = 0;
    double totalAvailableBandwidth = 0;
    int n = 0; // number of the surrounding nodes
    for (int i = 0; i < 20; ++i) if (isIn[i] && G[state][i]) {
        ++n;
        totalQueuingDelay += queuingDelay[state][i];
        totalTransmissionDelay += transmissionDelay[state][i];
        totalAvailableBandwidth += availableBandwidth[state][i];
    }

    // cost: delay
    double delay = 2 * atan(transmissionDelay[state][nextState] - totalTransmissionDelay / n) / pi;

    // cost: queue
    double queue = 2 * atan(queuingDelay[state][nextState] - totalQueuingDelay / n) / pi;

    // cost: loss
    double loss = 1 - 2 * loss[state][nextState];

    // cost: B1
    double B1 = 2 * availableBandwidth[state][nextState] / totalBandwidth[state][nextState];

    // cost: B2
    double B2 = 2 * atan(0.01 * (availableBandwidth[state][nextState] - totalAvailableBandwidth / n)) / pi;


    return -3 + beta1 * (theta1 * delay + theta2 * queue) + beta2 * loss + beta3 * (phi1 * B1 + phi2 * B2);

}

void domain_controller::sarsa(double (& q)[20][20]) {
    ++visit[src][des];

    if (visit[src][des] == 1) {
        // initialization
        for (int i = 0; i < 20; ++i) {
            for (int j = 0; j < 20; ++j) {
                q[i][j] = 0;
            }
        }
    }

    if (visit[src][des] == 101) visit[src][des] = 1;

    int state = src, action, nextState, nextAction;
    action = makeSoftmaxPolicy(nowState, q[nowState]);
    double reward;
    while (true) {
        nex[src][des][state] = action;
        nextState = action;
        nextAction = makeSoftmaxPolicy(nextState, q[nextState]);

        reward = getReward(state, action);
        q[state][action] += alpha * (reward + gamma * q[nextState][nextAction] - q[state][action]);

        if (nextState = des) break;
        action = nextAction;
        state = NextState;
    }
}

void domain_controller::getIsIn() {
    memset(isIn, 0, sizeof(isIn));
    num = 0;

    bool flag;
    for (int i = 0; i < 20; ++i) {
        flag = false;
        for (int j = 0; j < 20; ++j) if (G[i][j]) {
            flag = true;
            break;
        }
        isIn[i] = flag;
        num += flag;
    }
}

void domain_controller::initialize()
{
    for (int i = 0; i < 20; ++i) {
        for (int j = 0; j < 20; ++j) {
            loss[i][j] = transmissionDelay[i][j] = -1;
            queuingDelay[i][j] = availableBandwidth[i][j] = -1;
            totalBandwidth[i][j] = nex[i][j] = -1;
            visit[i][j] = 0;
        }
        isIn[i] = 0;
    }
}

// this part will be updated later
void domain_controller::handleMessage(cMessage *msg)
{
    string from = msg->getSenderModule()->getName();
    string name = msg->getName();

    if (from == "switches") {
        switch_message* tempmsg = check_and_cast<switch_message *>(msg);
        // retrieve network condition from slave controller
        src = tempmsg->getSource();
        des = tempmsg->getDestination();

        forwardMessageToSlave();

    } else if (from == "slave") {
        get++;
        retrieveCondition(msg);
        if (get == 2) { // all network information received
            get = 0;
            if (isIn[src] && isIn[des]) {
                // perform RL algorithm or push the message to super controller
                sarsa(q[src][des]);

                //send route plan to switch


            } else {
                // otherwise, send this request to super controller
                switch_message* msg_ = new switch_message("request");
                msg_->setSource(src);
                msg_->setDestination(des);

                send(msg_->dup(), "super");
            }

        }

    } else if (from == "super") {
        cMessage* msg_ = new cMessage("retrieve");
        condition* cond = new condition();
        copyCondition(cond);
        msg_->addObject(cond);
        send(msg_->dup(), "super");
    }
}

void domain_controller::forwardMessageToSlave()
{
    cMessage* msg = new cMessage();
    send(msg->dup(), "slave$o", 0);
    send(msg->dup(), "slave$o", 1);
}


void domain_controller::copyCondition(condition* cond)
{
    for (int i = 0; i < 20; ++i) {
            for (int j = 0; j < 20; ++j) {
                cond->loss[i][j] = loss[i][j];
                cond->transmissionDelay[i][j] = transmissionDelay[i][j];
                cond->queuingDelay[i][j] = queuingDelay[i][j];
                cond->availableBandwidth[i][j] = availableBandwidth[i][j];
                cond->totalBandwidth[i][j] = totalBandwidth[i][j];
                cond->G[i][j] = G[i][j];
            }
        }
}

void domain_controller::retrieveCondition(cMessage* msg) {
    condition* cond = (condition*) msg->getObject("");
    for (int i = 0; i < 20; ++i) {
            for (int j = 0; j < 20; ++j) {
                loss[i][j] = cond->loss[i][j];
                transmissionDelay[i][j] = cond->transmissionDelay[i][j];
                queuingDelay[i][j] = cond->queuingDelay[i][j];
                availableBandwidth[i][j] = cond->availableBandwidth[i][j];
                totalBandwidth[i][j] = cond->totalBandwidth[i][j];
                G[i][j] = cond->G[i][j];
            }
        }

    getIsIn();
}