/* EPANET 3.1.1 Pressure Management Extension
 *
 * Copyright (c) 2016 Open Water Analytics
 * Licensed under the terms of the MIT License (see the LICENSE file for details).
 *
 */

//! \file valve.h
//! \brief Describes the Valve class.

#ifndef VALVE_H_
#define VALVE_H_

#include "Elements/link.h"

#include <string>
#include <vector>

class Network;
class Pattern;

//! \class Valve
//! \brief A Link that controls flow or pressure.
//! \note Isolation (or shutoff) valves can be modeled by setting a
//!       pipe's Status property to OPEN or CLOSED.

class Valve: public Link
{
  public:

    enum ValveType {
        PRV,               //!< pressure reducing valve
        PSV,               //!< pressure sustaining valve
        FCV,               //!< flow control valve
        TCV,               //!< throttle control valve
        PBV,               //!< pressure breaker valve
        GPV,                //!< general purpose valve
		CCV,                //!< closure control valve
		DPRV                //!< Dynamic Pressure Reducing Valve
    };
    static const char* ValveTypeWords[];

	enum PresManagType {
		FO,                //!< Fixed Outlet Pressure Management
		TM,                //!< Pressure Management with Time Based Modulation
		FM,                //!< Pressure Management with Flow Based Modulation
		RNM                //!< Pressure Management with Remote Node Based Modulation
	};
	static const char* PresManagWords[];

    // Constructor/Destructor
    Valve(std::string name_);
    ~Valve();

    // Methods
    int         type();
    std::string typeStr();
	std::string typeStrPM();
    void        convertUnits(Network* nw);
    double      convertSetting(Network* nw, double s);

    void        setInitFlow();
    void        setInitStatus(int s);
    void        setInitSetting(double s);
	void		setLossFactor();
    void        initialize(bool initFlow);

    bool        isPRV();
    bool        isPSV();

	void        findHeadLoss(Network* nw, double q);
    void        updateStatus(double q, double h1, double h2);
    bool        changeStatus(int newStatus,
                             bool makeChange,
                             const std::string reason,
                             std::ostream& msgLog);
    bool        changeSetting(double newSetting,
                              bool makeChange,
                              const std::string reason,
                              std::ostream& msgLog);
    void        validateStatus(Network* nw, double qTol);
	bool	    makeChange;

    double      getVelocity();
    double      getRe(const double q, const double viscos);
    double      getSetting(Network* nw);

    // Properties
    ValveType   valveType;                                      //!< valve type
    double      lossFactor;                                     //!< minor loss factor
	Pattern*    settingPattern;                                 //!< Setting Pattern
	void        applyControlPattern(std::ostream& msgLog);
	PresManagType presManagType;                                //!< pressure management type
	double         fixedOutletPressure;                         //!< Fixed Outlet Pressure Value
	double         dayPressure;                                 //!< Day Pressure in Time Modulated PM
	double         nightPressure;                               //!< Night Pressure in Time Modulated PM
	double         a_FM;                                        //!< The first coefficient in the Flow Modulated PM
	double         b_FM;                                        //!< The second coefficient in the Flow Modulated PM
	double         c_FM;                                        //!< The third coefficient in the Flow Modulated PM
	double         rnmPressure;                                 //!< The pressure value of critical node in RNM
	Node*          remoteNode;                                  //!< pointer to the link's remote node
	double         dprvOutletPressure;
	double         Xm;
	double 		   delta_Xm;
	double		   Xm_Last;
	double		   errorValve;
	double		   errorSumValve;
	double		   errorDifValve;
	double         errorPreValve;


  protected:
    void        findOpenHeadLoss(double q);
    void        findPbvHeadLoss(double q);
    void        findTcvHeadLoss(double q);
    void        findGpvHeadLoss(Network* nw, double q);
    void        findFcvHeadLoss(double q);
	void        findCcvHeadLoss(Network* nw, double q);
	void        findDprvHeadLoss(Network* nw, double q);
    int         updatePrvStatus(double q, double h1, double h2);
	int         updateDprvStatus(double q, double h1, double h2);
    int         updatePsvStatus(double q, double h1, double h2);

    bool        hasFixedStatus;   //!< true if Open/Closed status is fixed
    double      elev;             //!< elevation of PRV/PSV valve
};

//-----------------------------------------------------------------------------
//    Inline Functions
//-----------------------------------------------------------------------------

inline
int  Valve::type() { return Link::VALVE; }

inline
bool Valve::isPRV() { return valveType == PRV; }

inline
bool Valve::isPSV() { return valveType == PSV; }

#endif
