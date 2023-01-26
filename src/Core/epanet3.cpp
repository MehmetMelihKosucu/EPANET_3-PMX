/* EPANET 3.1.1 Pressure Management Extension
 *
 * Copyright (c) 2016 Open Water Analytics
 * Distributed under the MIT License (see the LICENSE file for details).
 *
 */

///////////////////////////////////////////////
// Implementation of EPANET 3.1's API library  //
///////////////'''''///////////////////////////

// TO DO:
// - finish implementing all of the functions declared in EPANET3.H
// - provide a brief comment on what each function does

#include "epanet3.h"
#include "Core/project.h"
#include "Core/datamanager.h"
#include "Core/constants.h"
#include "Core/error.h"
#include "Core/network.h"
#include "Core/hydengine.h"
#include "Core/hydbalance.h"
#include "Elements/valve.h"
#include "Elements/pipe.h"
#include "Elements/pump.h"
#include "Elements/valve.h"
#include "Elements/pumpcurve.h"
#include "Elements/control.h"
#include "Elements/junction.h"
#include "Elements/tank.h"
#include "Elements/link.h"
#include "Utilities/utilities.h"
#include "rwcggasolver.h"
#include "matrixsolver.h"
#include "linkparser.h"

#include <cstring>
#include <cmath>
#include <limits>
#include <iostream>   //for debugging
#include <iomanip>
#include <algorithm>
#include <string>
#include <time.h>
#include <vector>


using namespace Epanet;
using namespace std;

#define project(p) ((Project *)p)

extern "C" {

//-----------------------------------------------------------------------------

int EN_getVersion(int* version)
{
    *version = VERSION;
    return 0;
}

//-----------------------------------------------------------------------------

int EN_runEpanet(const char* inpFile, const char* rptFile, const char* outFile)
{
    std::cout << "\n... EPANET Version 3.0\n";

    // ... declare a Project variable and an error indicator
    Project p;
    int err = 0;

	std::ofstream pressureAndFlowOutFile("hk-Result.txt"); // The directory could be changed according to your PC.

	std::ofstream valveOpeningFile("Xm-Result.txt");  // */

	double alfaopen = 0.000001;
	double alfaclose = 0.000001;

	int IndexV1, IndexJ1, Index13150, Index12957, IndexJ1552; // Hadımköy
	double flowV1, presJ1, pres13150, pres12957, pres1552;

	pressureAndFlowOutFile << "Time" << "\t\t" << "Inlet_Flow_Rate_(l/s)" << "\t\t" << "Pressure_1_(m)" << "\t\t" << "Pressure_13150_(m)" << "\t\t" << "Pressure_12957_(m)" << "\t\t" << "Pressure_1552_(m)" << "\t\t" << "Leakage_(l/s)" << "\n";

	// Output files in text format, and variables which are existing in output file 

    // ... initialize execution time clock
    clock_t start_t = clock();

	double totalLoss = 0;

	double totalFlow = 0;

	// PID Parameters for control

	double Kp, Ki, Kd;

	Kp = -0.000001365;
	Ki = 0.000000104;
	Kd = 0.00000067527;

    for (;;)
    {
        // ... open the command line files and load network data
        if ( (err = p.openReport(rptFile)) ) break;
        std::cout << "\n    Reading input file ...";
        if ( (err = p.load(inpFile)) ) break;
        if ( (err = p.openOutput(outFile)) ) break;
        p.writeSummary();

        // ... initialize the solver
        std::cout << "\n    Initializing solver ...";
        if ( (err = p.initSolver(false)) ) break;
        std::cout << "\n    ";

        // ... step through each time period
        int t = 0;
        int tstep = 0;

		double maxPres = 0, everMaxPres = 0;

        do
        {
            std::cout << "\r    Solving network at "                     //r
                << Utilities::getTime(t+tstep) << " hrs ...        ";	
	
			p.pressureManagement(t, valveOpeningFile, alfaopen, alfaclose, Kp, Ki, Kd);
	
            // ... run solver to compute hydraulics
            err = p.runSolver(&t);
            //p.writeMsgLog();
			
            // ... advance solver to next period in time while solving for water quality
            if ( !err ) err = p.advanceSolver(&tstep);

			totalLoss = p.computeWaterLoss(totalLoss);

			// Hadımköy WDN

			int ErrorV1 = EN_getLinkIndex("1", &IndexV1, p.getNetwork());
			int ErrorJ1 = EN_getNodeIndex("1", &IndexJ1, p.getNetwork());
			int Error13150 = EN_getNodeIndex("13150", &Index13150, p.getNetwork());
			int Error12957 = EN_getNodeIndex("12957", &Index12957, p.getNetwork());
			int ErrorJ1552 = EN_getNodeIndex("1552", &IndexJ1552, p.getNetwork());

			double ErrorValV1 = EN_getLinkValue(IndexV1, EN_FLOW, &flowV1, p.getNetwork());
			double ErrorValJ1 = EN_getNodeValue(IndexJ1, EN_PRESSURE, &presJ1, p.getNetwork());
			double ErrorVal13150 = EN_getNodeValue(Index13150, EN_PRESSURE, &pres13150, p.getNetwork());
			double ErrorVal12957 = EN_getNodeValue(Index12957, EN_PRESSURE, &pres12957, p.getNetwork()); 
			double ErrorValJ1552 = EN_getNodeValue(IndexJ1552, EN_PRESSURE, &pres1552, p.getNetwork());

			totalFlow += flowV1*tstep * 365 / (7 * 1000); // Compute total annual inlet water volume (m3)
			
			if ( t%30 == 0) // Extract the output parameters per 30 seconds.
				pressureAndFlowOutFile << Utilities::getTime(t) << "\t\t" << flowV1 << "\t\t" << presJ1 << "\t\t" << pres13150 << "\t\t" << pres12957 << "\t\t" << pres1552 << "\t\t" << totalLoss << "\n"; // */ 

			p.lasting();
			
        } while (tstep > 0 && !err );

		pressureAndFlowOutFile << totalFlow;

        break;
    }


    // ... simulation was successful
    if ( !err )
    {
        // ... report execution time
        clock_t end_t = clock();
        double cpu_t = ((double) (end_t - start_t)) / CLOCKS_PER_SEC;
        std::stringstream ss;
        ss << "\n  Simulation completed in ";
        p.writeMsg(ss.str());
        ss.str("");
        if ( cpu_t < 0.001 ) ss << "< 0.001 sec.";
        else ss << std::setprecision(3) << cpu_t << " sec.";
        p.writeMsg(ss.str());

        // ... report simulation results
        std::cout << "\n    Writing report ...                           ";
        err = p.writeReport();
        std::cout << "\n    Simulation completed.                         \n";
        std::cout << "\n... EPANET completed in " << ss.str() << "\n"; //
	} 
	
    if ( err )
    {
        p.writeMsgLog();
        std::cout << "\n\n    There were errors. See report file for details.\n";
        return err;
    } 
    return 0;
}

//-----------------------------------------------------------------------------

EN_Project EN_createProject()
{
    Project* p = new Project();
    return (EN_Project *)p;
}

//-----------------------------------------------------------------------------

int EN_deleteProject(EN_Project p)
{
    delete (Project *)p;
    return 0;
}

//-----------------------------------------------------------------------------

int EN_loadProject(const char* fname, EN_Project p)
{
    return project(p)->load(fname);
}

//-----------------------------------------------------------------------------

int EN_saveProject(const char* fname, EN_Project p)
{
    return project(p)->save(fname);
}

//-----------------------------------------------------------------------------

int EN_clearProject(EN_Project p)
{
    project(p)->clear();
    return 0;
}

//-----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////
//  NOT SURE IF THIS METHOD WORKS CORRECTLY -- NEEDS TESTING  //
////////////////////////////////////////////////////////////////
int EN_cloneProject(EN_Project pClone, EN_Project pSource)
{
    if ( pSource == nullptr || pClone == nullptr ) return 102;
    int err = 0;
    std::string tmpFile;
    if ( Utilities::getTmpFileName(tmpFile) )
    {
        try
        {
            EN_saveProject(tmpFile.c_str(), pSource);
            EN_loadProject(tmpFile.c_str(), pClone);
        }
        catch (ENerror const& e)
        {
	        project(pSource)->writeMsg(e.msg);
            err = e.code;
  	    }
        catch (...)
        {
            err = 208; //Unspecified error
        }
        if ( err > 0 )
        {
            EN_clearProject(pClone);
        }
        remove(tmpFile.c_str());
        return err;
    }
    return 208;
}

//-----------------------------------------------------------------------------

int EN_runProject(EN_Project p)    // <<=============  TO BE COMPLETED
{
    return 0;
}

//-----------------------------------------------------------------------------

int EN_initSolver(int initFlows, EN_Project p)
{
    return project(p)->initSolver(initFlows);
}

//-----------------------------------------------------------------------------

int EN_runSolver(int* t, EN_Project p)
{
	return project(p)->runSolver(t);
}

//-----------------------------------------------------------------------------

int EN_advanceSolver(int *dt, EN_Project p)
{
    return project(p)->advanceSolver(dt);
}

//-----------------------------------------------------------------------------

int EN_openOutputFile(const char* fname, EN_Project p)
{
    return project(p)->openOutput(fname);
}

//-----------------------------------------------------------------------------

int EN_saveOutput(EN_Project p)
{
    return project(p)->saveOutput();
}

//-----------------------------------------------------------------------------

int EN_openReportFile(const char* fname, EN_Project p)
{
    return project(p)->openReport(fname);
}

//-----------------------------------------------------------------------------

int EN_writeReport(EN_Project p)
{
    return project(p)->writeReport();
}

//-----------------------------------------------------------------------------

int EN_writeSummary(EN_Project p)
{
    project(p)->writeSummary();
    return 0;
}

//-----------------------------------------------------------------------------

int EN_writeResults(int t, EN_Project p)
{
    project(p)->writeResults(t);
    return 0;
}

//-----------------------------------------------------------------------------

int EN_writeMsgLog(EN_Project p)
{
    project(p)->writeMsgLog();
    return 0;
}

//-----------------------------------------------------------------------------

int EN_getCount(int element, int* result, EN_Project p)
{
    return DataManager::getCount(element, result, project(p)->getNetwork());
}

//-----------------------------------------------------------------------------

int EN_getNodeIndex(char* name, int* index, EN_Project p)
{
    return DataManager::getNodeIndex(name, index, project(p)->getNetwork());
}

//-----------------------------------------------------------------------------

int EN_getNodeId(int index, char* id, EN_Project p)
{
    return DataManager::getNodeId(index, id, project(p)->getNetwork());
}

//-----------------------------------------------------------------------------

int EN_getNodeType(int index, int* type, EN_Project p)
{
    return DataManager::getNodeType(index, type, project(p)->getNetwork());
}

//-----------------------------------------------------------------------------

int EN_getNodeValue(int index, int param, double* value, EN_Project p)
{
    return DataManager::getNodeValue(index, param, value, project(p)->getNetwork());
}

//-----------------------------------------------------------------------------

int EN_getLinkIndex(char* name, int* index, EN_Project p)
{
    return DataManager::getLinkIndex(name, index, project(p)->getNetwork());
}

//-----------------------------------------------------------------------------

int EN_getLinkId(int index, char* id, EN_Project p)
{
    return DataManager::getLinkId(index, id, project(p)->getNetwork());
}

//-----------------------------------------------------------------------------

int EN_getLinkType(int index, int* type, EN_Project p)
{
    return DataManager::getLinkType(index, type, project(p)->getNetwork());
}

//-----------------------------------------------------------------------------

int EN_getLinkNodes(int index, int* fromNode, int* toNode, EN_Project p)
{
    return DataManager::getLinkNodes(index, fromNode, toNode,
                                     project(p)->getNetwork());
}

//-----------------------------------------------------------------------------

int EN_getLinkValue(int index, int param, double* value, EN_Project p)
{
   return DataManager::getLinkValue(index, param, value, project(p)->getNetwork());
}

int EN_setLinkValue(int index, int param, double value, EN_Project p)
{
	return DataManager::setLinkValue(index, param, value, project(p)->setNetwork());
}


}  // end of namespace
