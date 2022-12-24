/* EPANET 3.1.1 Pressure Management Extension
 *
 * Copyright (c) 2016 Open Water Analytics
 * Licensed under the terms of the MIT License (see the LICENSE file for details).
 *
 */

#include "Core/project.h"
#include "valve.h"
#include "pattern.h"
#include "node.h"
#include "curve.h"
#include "Core/network.h"
#include "Core/constants.h"
#include "Models/headlossmodel.h"

#include <cmath>
#include <algorithm>
#include <vector>
using namespace std;

// Two new valve type is introduced to EPANET. The name of one is Closure Control Valve (CCV), and the other is Dynamic Pressure Reducing Valve (DPRV).

const char* Valve::ValveTypeWords[] = {"PRV", "PSV", "FCV", "TCV", "PBV", "GPV", "CCV", "DPRV"}; 

const char* Valve::PresManagWords[] = {"FO", "TM", "FM", "RNM"};

const double MIN_LOSS_COEFF = 0.1;     // default minor loss coefficient

//-----------------------------------------------------------------------------

//  Constructor/Destructor

Valve::Valve(string name_) :
    Link(name_),
    valveType(TCV),
    lossFactor(0.0),
	remoteNode(nullptr),
	settingPattern(0),
    hasFixedStatus(false),
    elev(0.0)
{
    initStatus = VALVE_ACTIVE;
    initSetting = 0;
}

Valve::~Valve() {}

//-----------------------------------------------------------------------------

//  Return the string representation of the valve's type.

string Valve::typeStr()
{
    return ValveTypeWords[valveType];
}

//-----------------------------------------------------------------------------

string Valve::typeStrPM()
{
	return PresManagWords[presManagType];
}

//  Convert a valve's properties from user to internal units.

void Valve::convertUnits(Network* nw)
{
    // ... convert diameter units
    diameter /= nw->ucf(Units::DIAMETER);

    // ... apply a minimum minor loss coeff. if necessary
    double c = lossCoeff;
    if ( c < MIN_LOSS_COEFF ) c = MIN_LOSS_COEFF;

    // ... convert minor loss from V^2/2g basis to Q^2 basis
    lossFactor = 0.02517 * c / pow(diameter, 4);

    // ... convert initial valve setting units
    initSetting = convertSetting(nw, initSetting);
}

//-----------------------------------------------------------------------------

//  Convert the units of a valve's flow or pressure setting.

double Valve::convertSetting(Network* nw, double s)
{
    switch (valveType)
    {
    // ... convert pressure valve setting
    case PRV:
    case PSV:
    case PBV: s /= nw->ucf(Units::PRESSURE); break;

    // ... convert flow valve setting
    case FCV: s /= nw->ucf(Units::FLOW); break;

    default: break;
    }
    if (valveType == PRV) elev = toNode->elev;
	if (valveType == DPRV) elev = toNode->elev;
    if (valveType == PSV) elev = fromNode->elev;
    return s;
}

//-----------------------------------------------------------------------------

//  Set a valve's initial status.

void Valve::setInitStatus(int s)
{
    initStatus = s;
    hasFixedStatus = true;
}

//-----------------------------------------------------------------------------

//  Set a valve's initial setting.

void Valve::setInitSetting(double s)
{
    initSetting = s;
    initStatus = VALVE_ACTIVE;
    hasFixedStatus = false;
}

void Valve::setLossFactor()
{
	lossFactor = 0.02517 * lossCoeff / pow(diameter, 4);
}

//-----------------------------------------------------------------------------

//  Initialize a valve's state.

void Valve::initialize(bool reInitFlow)
{
    status = initStatus;
    setting = initSetting;
    if ( reInitFlow ) setInitFlow();
    hasFixedStatus = (initStatus != VALVE_ACTIVE);
}

//-----------------------------------------------------------------------------

//  Initialize a valve's flow rate.

void Valve::setInitFlow()
{
    // ... flow at velocity of 1 ft/s
    flow = PI * diameter * diameter / 4.0;
    if (valveType == FCV)
    {
        flow = setting;
    }
	else if (valveType == CCV)
	{
		if (setting == 0) flow = ZERO_FLOW;
		else flow = PI * diameter * diameter / 4.0; 
	} 
	else if (valveType == DPRV)
	{
		if (setting == 0) flow = ZERO_FLOW;
		else flow = PI * diameter * diameter / 4.0;
	}
	// */
	pastFlow = 0;
	pastHloss = 0;
	pastSetting = 0;
}

//-----------------------------------------------------------------------------

//  Find the flow velocity through a valve.

double Valve::getVelocity()
{
    double area = PI * diameter * diameter / 4.0;
    return flow / area;
}

//-----------------------------------------------------------------------------

//  Find the Reynolds Number of flow through a valve.

double Valve::getRe(const double q, const double viscos)
{
    return  abs(q) / (PI * diameter * diameter / 4.0) * diameter / viscos;
}

//-----------------------------------------------------------------------------

//  Return a valve's setting in user units.

double Valve::getSetting(Network* nw)
{
    switch(valveType)
    {
    case PRV:
    case PSV:
    case PBV: return setting * nw->ucf(Units::PRESSURE);
    case FCV: return setting * nw->ucf(Units::FLOW);
    default:  return setting;
    }
}

//-----------------------------------------------------------------------------

//  Find a valve's head loss and its gradient.

void Valve::findHeadLoss(Network* nw, double q)
{
    hLoss = 0.0;
	
    hGrad = 0.0;

    // ... valve is temporarily closed (e.g., tries to drain an empty tank)

    if ( status == TEMP_CLOSED)
    {
        HeadLossModel::findClosedHeadLoss(q, hLoss, hGrad);

		inertialTerm = MIN_GRADIENT;
    }

    // ... valve has fixed status (OPEN or CLOSED)

    else if ( hasFixedStatus )
    {
        if (status == LINK_CLOSED)
        {
            HeadLossModel::findClosedHeadLoss(q, hLoss, hGrad);
			inertialTerm = MIN_GRADIENT;
        }
		else if (status == LINK_OPEN)
		{
			findOpenHeadLoss(q);
			inertialTerm = MIN_GRADIENT;
		}	
    }

    // ... head loss for active valves depends on valve type

    else switch (valveType)
    {
	case PBV: findPbvHeadLoss(q); inertialTerm = MIN_GRADIENT; break;
	case TCV: findTcvHeadLoss(q); inertialTerm = MIN_GRADIENT; break;
	case CCV: 
		if(setting == 0)
		{
			status = LINK_CLOSED;
			HeadLossModel::findClosedHeadLoss(q, hLoss, hGrad);
			inertialTerm = MIN_GRADIENT;
		}
		else if (setting != 0)
		{
			status = LINK_OPEN;
			findCcvHeadLoss(nw, q); 
			inertialTerm = 10.765 / (32.174 * PI * diameter * diameter); // Approximate value for valve inertial term
		}
		break;
	case DPRV:
		if (status == LINK_CLOSED || Xm == 0) 
		{
			HeadLossModel::findClosedHeadLoss(q, hLoss, hGrad);
			inertialTerm = MIN_GRADIENT;
		}
		else if (status == LINK_OPEN) 
		{
			findOpenHeadLoss(q);
			inertialTerm = 0;
		}
		else
		{
			findDprvHeadLoss(nw, q);
			inertialTerm = 0;
		}
		break;
	case GPV: findGpvHeadLoss(nw, q); inertialTerm = MIN_GRADIENT; break;
	case FCV: findFcvHeadLoss(q); inertialTerm = MIN_GRADIENT; break;

    // ... PRVs & PSVs without fixed status can be either
    //     OPEN, CLOSED, or ACTIVE.
    case PRV:
    case PSV:
        if ( status == LINK_CLOSED )
        {
            HeadLossModel::findClosedHeadLoss(q, hLoss, hGrad);
			inertialTerm = MIN_GRADIENT;
        }
        else if ( status == LINK_OPEN ) findOpenHeadLoss(q);
		inertialTerm = MIN_GRADIENT;
        break;
    }
}

//-----------------------------------------------------------------------------

//  Find the head loss and its gradient for a fully open valve.

void Valve::findOpenHeadLoss(double q)
{
    hGrad = 2.0 * lossFactor * abs(q);
    if ( hGrad < MIN_GRADIENT )
    {
        hGrad = MIN_GRADIENT;
        hLoss = hGrad * q;
    }
    else hLoss = hGrad * q / 2.0;
}

//-----------------------------------------------------------------------------

//  Find the head loss and its gradient for a pressure breaker valve.

void Valve::findPbvHeadLoss(double q)
{
    // ... treat as open valve if minor loss > valve setting

    double mloss = lossFactor * q * q;
    if ( mloss >= abs(setting) ) findOpenHeadLoss(q);

    // ... otherwise force head loss across valve equal to setting

    else
    {
        hGrad = MIN_GRADIENT;
        hLoss = setting;
    }
}

//-----------------------------------------------------------------------------

//  Find the head loss and its gradient for a throttle control valve.

void Valve::findTcvHeadLoss(double q)
{
    //... save open valve loss factor

    double f = lossFactor;

    // ... convert throttled loss coeff. setting to a loss factor

    double d2 = diameter * diameter;
    lossFactor = 0.025173 * setting / d2 / d2;

    // ... throttled loss coeff. can't be less than fully open coeff.

    lossFactor = max(lossFactor, f);

    // ... use the setting's loss factor to compute head loss

    findOpenHeadLoss(q);

    // ... restore open valve loss factor

    lossFactor = f;
}

//-----------------------------------------------------------------------------

//  Find the head loss and its gradient for a closure control valve.

void Valve::findCcvHeadLoss(Network* nw, double q)
{
	//... save open valve loss factor

	double f = lossFactor;

	if (nw->option(Options::VALVE_REP_TYPE) == "Toe")
	{
		double valve_conductance = 16.96; // ft^2.5 / s =   0.87 m^(2.5) / s 
		double vc_square = valve_conductance * valve_conductance;
		double Toe = setting; // Globe Valve
		double Toe_Square = Toe * Toe;
		lossFactor = 1 / (vc_square * Toe_Square); // (Nault and Karney, 2016) */
	}

	else if (nw->option(Options::VALVE_REP_TYPE) == "Cd")
	{
		double d2 = diameter * diameter;
		double FullArea = (d2 * PI) / 4;           // Area of valve
		//double Area = FullArea * (-0.3575 * pow(setting, 4) + 0.2766 * pow(setting, 3) - 0.2233 * pow(setting, 2) + 1.3056 * setting - 0.0005);
		double FullArea2 = FullArea * FullArea;
		double Cd = -1.1293 * pow(setting, 6) + 3.3823 * pow(setting, 5) - 3.443 * pow(setting, 4) + 0.5671 * pow(setting, 3) + 1.0371 * pow(setting, 2) - 0.0037 * setting; // Globe Valve according to Tullis (1989)
		double Cd2 = Cd * Cd;
		double g = 32.174; // gravitational acceleration
		lossFactor = (1 / Cd2 - 1) * (1 / (2 * g * FullArea2)); // */
	}
	
	// setting value is between 0 and 1

	findOpenHeadLoss(q);
	
	// ... restore open valve loss factor

	//lossFactor = f;
}

void Valve::findDprvHeadLoss(Network* nw, double q)
{
	//... save open valve loss factor

	double f = lossFactor;

	double Cv;
	
	double Xm2 = Xm * Xm;
	double Xm3 = Xm2 * Xm;
	double Xm4 = Xm2 * Xm2;
	double Xm5 = Xm3 * Xm2;
	double Xm6 = Xm3 * Xm3;

	double k1 = 0.09;
	double k2 = -1.21;
	double k3 = 2.33;
	double k4 = -0.21;
	double Cvmax = 1.442760731; // medium  0.523559646; // = 0.02685    // 1.442760731; // = 0.074 m^5/2;
	double Cvtr = 0.07550186203; // 0.0014051;   // 0.07550186203; // 0.00387253248

	if (0 <= Xm && Xm < 0.12)
	{
		Cv = Cvtr * Xm / 0.12;
	}

	// Xm value is between 0 and 1

	else if (0.12 <= Xm && Xm <= 1.0)
	{
		Cv = (k1 * Xm3 + k2 * Xm2 + k3 * Xm + k4) * Cvmax;
	} // */

	//Cv = 0.02107 - 0.02962*exp(-51.1322*Xm) + 0.0109*exp(-261*Xm)-0.00325*exp(-683.17*Xm)+0.0009*exp(-399.5*Xm) ;

	//Cv = -0.1044806762 * xm6 + 0.3488936581 * xm5 - 0.4398537532 * xm4 + 0.2449207578 * xm3 - 0.0449232271 * xm2 + 0.0058813428 * xm + 0.0000244728;

	double Cv_square = Cv * Cv;
	lossFactor = 1 / Cv_square;

	//double Kv = -0.00001201 * Xm3 + 0.003162 * Xm2 - 0.3186 * Xm + 12.23;
	
	//double Kv = 0.1597 * Xm2 - 0.01129 * Xm;

	//lossFactor = 1 / Kv;

	findOpenHeadLoss(q);

	// ... restore open valve loss factor

	lossFactor = f; // */
}

//  Find the head loss and its derivative for a general purpose valve.

void Valve::findGpvHeadLoss(Network* nw, double q)
{
    // ... retrieve head loss curve for valve

    int curveIndex = (int)setting;
    Curve* curve = nw->curve(curveIndex);

    // ... retrieve units conversion factors (curve is in user's units)

    double ucfFlow = nw->ucf(Units::FLOW);
    double ucfHead = nw->ucf(Units::LENGTH);

    // ... find slope (r) and intercept (h0) of curve segment

    double qRaw = abs(q) * ucfFlow;
    double r, h0;
    curve->findSegment(qRaw, r, h0);

    // ... convert to internal units

    r *= ucfFlow / ucfHead;
    h0 /= ucfHead;

    // ... determine head loss and derivative for this curve segment

    hGrad = r; //+ 2.0 * lossFactor * abs(q);
    hLoss = h0 + r * abs(q); // + lossFactor * q * q;
    if ( q < 0.0 ) hLoss = -hLoss;
}

//-----------------------------------------------------------------------------

//  Find the head loss and its gradient for a flow control valve.

void Valve::findFcvHeadLoss(double q)
{
    double xflow = q - setting;    // flow in excess of the setting

    // ... apply a large head loss factor to the flow excess

    if (xflow > 0.0)
    {
        hLoss = lossFactor * setting * setting + HIGH_RESISTANCE * xflow;
        hGrad = HIGH_RESISTANCE;
    }

    // ... otherwise treat valve as an open valve

    else
    {
        if ( q < 0.0 )
        {
            HeadLossModel::findClosedHeadLoss(q, hLoss, hGrad);
        }
        else findOpenHeadLoss(q);
    }
}

//-----------------------------------------------------------------------------

//  Update a valve's status.

void Valve::updateStatus(double q, double h1, double h2)
{
    if ( hasFixedStatus ) return;
    int newStatus = status;

    switch ( valveType )
    {
        case PRV: newStatus = updatePrvStatus(q, h1, h2); break;
		case DPRV: newStatus = updateDprvStatus(q, h1, h2); break;
        case PSV: newStatus = updatePsvStatus(q, h1, h2); break;
        default:  break;
    }

    if ( newStatus != status )
    {
        if ( newStatus == Link::LINK_CLOSED ) flow = ZERO_FLOW;
        status = newStatus;
    }
}

//-----------------------------------------------------------------------------

//  Update the status of a pressure reducing valve.

int Valve::updatePrvStatus(double q, double h1, double h2)
{
    int    s = status;                      // new valve status
    double hset = setting + elev;           // head setting

    switch ( status )
    {
    case VALVE_ACTIVE:
        if      ( q < -ZERO_FLOW ) s = LINK_CLOSED;
        else if ( h1 < hset )      s = LINK_OPEN;
        break;

    case LINK_OPEN:
        if      ( q < -ZERO_FLOW ) s = LINK_CLOSED;
        else if ( h2 > hset )      s = VALVE_ACTIVE;
        break;

    case LINK_CLOSED:
        if      ( h1 > hset && h2 < hset ) s = VALVE_ACTIVE;
        else if ( h1 < hset && h1 > h2 )   s = LINK_OPEN;
        break;
    }
    return s;
}

int Valve::updateDprvStatus(double q, double h1, double h2)
{
	int    s = status;						      // new valve status
	
	if (presManagType == Valve::FO)
	{
		dprvOutletPressure = fixedOutletPressure / 0.3048;
	}
	else
	{
		dprvOutletPressure = toNode->head - toNode->elev;
	}
	
	double hset = dprvOutletPressure + elev;      // head setting
	
	switch (status)
	{
	case VALVE_ACTIVE:
		if (q < -ZERO_FLOW ) 
			s = LINK_CLOSED;
		else if (h1 < hset)  
			s = LINK_OPEN;
		break;

	case LINK_OPEN:
		if (q < -ZERO_FLOW) s = LINK_CLOSED;
		else if (h2 > hset) 
			s = VALVE_ACTIVE;
		break;

	case LINK_CLOSED:
		if (h1 > hset && h2 < hset)  
			s = VALVE_ACTIVE;
		else if (h1 < hset && h1 > h2) 
			s = LINK_OPEN;
		break;
	}
	return s;
}

//-----------------------------------------------------------------------------

//  Update the status of a pressure sustaining valve.

int Valve::updatePsvStatus(double q, double h1, double h2)
{
    int    s = status;                      // new valve status
    double hset = setting + elev;           // head setting

    switch (status)
    {
    case VALVE_ACTIVE:
        if      (q < -ZERO_FLOW ) s = LINK_CLOSED;
        else if (h2 > hset )      s = LINK_OPEN;
        break;

    case LINK_OPEN:
        if      (q < -ZERO_FLOW ) s = LINK_CLOSED;
	    else if (h1 < hset )      s = VALVE_ACTIVE;
        break;

    case LINK_CLOSED:
        if      ( h2 < hset && h1 > hset) s = VALVE_ACTIVE;
        else if ( h2 > hset && h1 > h2 )  s = LINK_OPEN;
        break;
    }
    return s;
}

//-----------------------------------------------------------------------------

//  Change the setting of a valve.

bool Valve::changeSetting(double newSetting, bool makeChange, const string reason, ostream& msgLog)
{
	if (valveType == CCV)
	{
		if (setting != newSetting)
		{
			if (status == Link::LINK_CLOSED && newSetting == 0.0)
			{
				setting = newSetting;
				return false;
			}

			if (makeChange)
			{
				if (newSetting == 0.0)
				{
					status = Link::LINK_CLOSED;
					flow = ZERO_FLOW;
				}
				else status = Link::LINK_OPEN;
				msgLog << "\n    " << reason;
				setting = newSetting;
			}
			return true;
		}
		return false;
	}

	else
	{
		if (setting != newSetting)
		{
			if (status == Link::LINK_CLOSED) // && newSetting == 0.0)
			{
				setting = newSetting;
				return false;
			}

			if (makeChange)
			{
				if (newSetting == 0.0)
				{
					status = Link::LINK_CLOSED;
					flow = ZERO_FLOW;
				}
				else status = Link::LINK_OPEN;
				msgLog << "\n    " << reason;
				setting = newSetting;
			}
			return true;
		}
		return false;
	}
	
	
}

//-----------------------------------------------------------------------------

//  Change the status of a valve.

bool Valve::changeStatus(
        int newStatus, bool makeChange, const string reason, ostream& msgLog)
{
    if ( !hasFixedStatus || status != newStatus )
    {
        if ( makeChange )
        {
            msgLog << "\n    " << reason;
            status = newStatus;
            hasFixedStatus = true;
            if ( status == LINK_CLOSED ) flow = ZERO_FLOW;
        }
        return true;
    }
    return false;
}

//-----------------------------------------------------------------------------

//  Check for negative flow in a PRV/PSV valve (for debugging only)

void Valve::validateStatus(Network* nw, double qTol)
{
    switch (valveType)
    {
    case PRV:
    case PSV:
        if (flow < -qTol)
        {
            nw->msgLog << "\nValve " << name << " flow = " << flow*nw->ucf(Units::FLOW);
        }
        break;

    default: break;
    }
}

void Valve::applyControlPattern(ostream& msgLog)
{
	double fs= 1.0;
	if (settingPattern)
	{
		//changeSetting(settingPattern->currentFactor(), true, "setting pattern", msgLog);
		fs = settingPattern->currentFactor();
	}
	setting = setting * fs;
}
