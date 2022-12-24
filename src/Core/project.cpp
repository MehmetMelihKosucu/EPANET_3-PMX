/* EPANET 3.1.1 Pressure Management Extension
 *
 * Copyright (c) 2016 Open Water Analytics
 * Distributed under the MIT License (see the LICENSE file for details).
 *
 */

 /////////////////////////////////////////////
 //  Implementation of the Project class.  //
 ////////////////////////////////////////////

#include "project.h"
#include "epanet3.h"
#include "Core/error.h"
#include "Core/diagnostics.h"
#include "Core/datamanager.h"
#include "Core/constants.h"
#include "Core/error.h"
#include "Core/network.h"
#include "Core/hydengine.h"
#include "Core/hydbalance.h"
#include "Core/project.h"
#include "Elements/pipe.h"
#include "Elements/pump.h"
#include "Elements/valve.h"
#include "Elements/pumpcurve.h"
#include "Elements/control.h"
#include "Elements/junction.h"
#include "Elements/tank.h"
#include "Elements/link.h"
#include "Input/inputreader.h"
#include "Output/projectwriter.h"
#include "Output/reportwriter.h"
#include "Utilities/utilities.h"
#include "linkparser.h"
#include "rwcggasolver.h"
#include "matrixsolver.h"

#include <cstring>
#include <cmath>
#include <limits>
#include <iostream>   //for debugging
#include <iomanip>
#include <algorithm>
#include <string>
#include <time.h>
#include <fstream>
using namespace std;

//-----------------------------------------------------------------------------

namespace Epanet
{
	//  Constructor

	Project::Project() :
		inpFileName(""),
		outFileName(""),
		tmpFileName(""),
		rptFileName(""),
		networkEmpty(true),
		hydEngineOpened(false),
		qualEngineOpened(false),
		outputFileOpened(false),
		solverInitialized(false),
		runQuality(false)
	{
		Utilities::getTmpFileName(tmpFileName);
	}

	//  Destructor

	Project::~Project()
	{
		//cout << "\nDestructing Project.";

		closeReport();
		outputFile.close();
		remove(tmpFileName.c_str());

		//cout << "\nProject destructed.\n";
	}

	//-----------------------------------------------------------------------------

	//  Load a project from a file.

	int Project::load(const char* fname)
	{
		try
		{
			// ... clear any current project
			clear();

			// ... check for duplicate file names
			string s = fname;
			if (s.size() == rptFileName.size() && Utilities::match(s, rptFileName))
			{
				throw FileError(FileError::DUPLICATE_FILE_NAMES);
			}
			if (s.size() == outFileName.size() && Utilities::match(s, outFileName))
			{
				throw FileError(FileError::DUPLICATE_FILE_NAMES);
			}

			// ... save name of input file
			inpFileName = fname;

			// ... use an InputReader to read project data from the input file
			InputReader inputReader;
			inputReader.readFile(fname, &network);
			networkEmpty = false;
			runQuality = network.option(Options::QUAL_TYPE) != Options::NOQUAL;

			// ... convert all network data to internal units
			network.convertUnits();
			network.options.adjustOptions();
			return 0;
		}
		catch (ENerror const& e)
		{
			writeMsg(e.msg);
			return e.code;
		}
	}

	//-----------------------------------------------------------------------------

	//  Save the project to a file.

	int Project::save(const char* fname)
	{
		try
		{
			if (networkEmpty) return 0;
			ProjectWriter projectWriter;
			projectWriter.writeFile(fname, &network);
			return 0;
		}
		catch (ENerror const& e)
		{
			writeMsg(e.msg);
			return e.code;
		}
	}

	//-----------------------------------------------------------------------------

	//  Clear the project of all data.

	void Project::clear()
	{
		hydEngine.close();
		hydEngineOpened = false;

		qualEngine.close();
		qualEngineOpened = false;

		network.clear();
		networkEmpty = true;

		solverInitialized = false;
		inpFileName = "";
	}

	//-----------------------------------------------------------------------------

	//  Initialize the project's solvers.

	int Project::initSolver(bool initFlows)
	{
		try
		{
			if (networkEmpty) return 0;
			solverInitialized = false;
			Diagnostics diagnostics;
			diagnostics.validateNetwork(&network);

			// ... open & initialize the hydraulic engine
			if (!hydEngineOpened)
			{
				initFlows = true;
				hydEngine.open(&network);
				hydEngineOpened = true;
			}
			hydEngine.init(initFlows);

			// ... open and initialize the water quality engine
			if (runQuality == true)
			{
				if (!qualEngineOpened)
				{
					qualEngine.open(&network);
					qualEngineOpened = true;
				}
				qualEngine.init();
			}

			// ... mark solvers as being initialized
			solverInitialized = true;

			// ... initialize the binary output file
			outputFile.initWriter();
			return 0;
		}
		catch (ENerror const& e)
		{
			writeMsg(e.msg);
			return e.code;
		}
	}

	//-----------------------------------------------------------------------------

	//  Solve network hydraulics at the current point in time.

	int Project::runSolver(int* t)
	{
		try
		{
			if (!solverInitialized) throw SystemError(SystemError::SOLVER_NOT_INITIALIZED);
			hydEngine.solve(t);
			if (outputFileOpened  && *t % network.option(Options::REPORT_STEP) == 0)
			{
				outputFile.writeNetworkResults();
			}
			return 0; // */
		}
		catch (ENerror const& e)
		{
			writeMsg(e.msg);
			return e.code;
		}
	}

	//-----------------------------------------------------------------------------

	//  Advance the hydraulic solver to the next point in time while updating
	//  water quality.

	int Project::advanceSolver(int* dt)
	{
		try
		{
			// ... advance to time when new hydraulics need to be computed
			hydEngine.advance(dt);

			// ... if at end of simulation (dt == 0) then finalize results
			if (*dt == 0) finalizeSolver();

			// ... otherwise update water quality over the time step
			else if (runQuality) qualEngine.solve(*dt);
			return 0;
		}
		catch (ENerror const& e)
		{
			writeMsg(e.msg);
			return e.code;
		}
	}

	//-----------------------------------------------------------------------------

	//  Open a binary file that saves computed results.

	int Project::openOutput(const char* fname)
	{
		//... close an already opened output file
		if (networkEmpty) return 0;
		outputFile.close();
		outputFileOpened = false;

		// ... save the name of the output file
		outFileName = fname;
		if (strlen(fname) == 0) outFileName = tmpFileName;

		// ... open the file
		try
		{
			outputFile.open(outFileName, &network);
			outputFileOpened = true;
			return 0;
		}
		catch (ENerror const& e)
		{
			writeMsg(e.msg);
			return e.code;
		}
	}

	//-----------------------------------------------------------------------------

	//  Save results for the current time period to the binary output file.

	int Project::saveOutput()
	{
		if (!outputFileOpened) return 0;
		try
		{
			outputFile.writeNetworkResults();
			return 0;
		}
		catch (ENerror const& e)
		{
			writeMsg(e.msg);
			return e.code;
		}
	}

	//-----------------------------------------------------------------------------

	//  Finalize computed quantities at the end of a run

	void Project::finalizeSolver()
	{
		if (!solverInitialized) return;

		// Save energy usage results to the binary output file.
		if (outputFileOpened)
		{
			double totalHrs = hydEngine.getElapsedTime() / 3600.0;
			double peakKwatts = hydEngine.getPeakKwatts();
			outputFile.writeEnergyResults(totalHrs, peakKwatts);
		}

		// Write mass balance results for WQ constituent to message log
		if (runQuality && network.option(Options::REPORT_STATUS))
		{
			network.qualBalance.writeBalance(network.msgLog);
		}
	}

	//-----------------------------------------------------------------------------

	//  Open the project's status/report file.

	int  Project::openReport(const char* fname)
	{
		try
		{
			//... close an already opened report file
			if (rptFile.is_open()) closeReport();

			// ... check that file name is different from input file name
			string s = fname;
			if (s.size() == inpFileName.size() && Utilities::match(s, inpFileName))
			{
				throw FileError(FileError::DUPLICATE_FILE_NAMES);
			}
			if (s.size() == outFileName.size() && Utilities::match(s, outFileName))
			{
				throw FileError(FileError::DUPLICATE_FILE_NAMES);
			}

			// ... open the report file
			rptFile.open(fname);
			if (!rptFile.is_open())
			{
				throw FileError(FileError::CANNOT_OPEN_REPORT_FILE);
			}
			ReportWriter rw(rptFile, &network);
			rw.writeHeading();
			return 0;
		}
		catch (ENerror const& e)
		{
			writeMsg(e.msg);
			return e.code;
		}
	}

	//-----------------------------------------------------------------------------

	// Write a message to the project's message log.

	void  Project::writeMsg(const std::string& msg)
	{
		network.msgLog << msg;
	}

	//-----------------------------------------------------------------------------

	//  Write the project's title and option summary to the report file.

	void Project::writeSummary()
	{
		if (!rptFile.is_open()) return;
		ReportWriter reportWriter(rptFile, &network);
		reportWriter.writeSummary(inpFileName);
	}

	//-----------------------------------------------------------------------------

	//  Close the project's report file.

	void Project::closeReport()
	{
		if (rptFile.is_open()) rptFile.close();
	}

	//-----------------------------------------------------------------------------

	//  Write the project's message log to an output stream.

	void Project::writeMsgLog(ostream& out)
	{
		out << network.msgLog.str();
		network.msgLog.str("");
	}

	//-----------------------------------------------------------------------------

	//  Write the project's message log to the report file.

	void Project::writeMsgLog()
	{
		if (rptFile.is_open())
		{
			rptFile << network.msgLog.str();
			network.msgLog.str("");
		}
	}

	//-----------------------------------------------------------------------------

	//  Write results at the current time period to the report file.

	void Project::writeResults(int t)
	{
		if (!rptFile.is_open()) return;
		ReportWriter reportWriter(rptFile, &network);
		reportWriter.writeResults(t);
	}

	//-----------------------------------------------------------------------------

	//  Write all results saved to the binary output file to a report file.

	int Project::writeReport()
	{
		try
		{
			if (!outputFileOpened)
			{
				throw FileError(FileError::NO_RESULTS_SAVED_TO_REPORT);
			}
			ReportWriter reportWriter(rptFile, &network);
			reportWriter.writeReport(inpFileName, &outputFile);
			return 0;
		}
		catch (ENerror const& e)
		{
			writeMsg(e.msg);
			return e.code;
		}
	}

	void Project::pressureManagement(int t, ofstream& outFile, double alfaopen, double alfaclose, double Kp, double Ki, double Kd)
	{
		double ref;
		double pToNode;
		double pToPastNode;
		double pFromNode;
		double pRemoteNode;

		int deltat = network.option(Options::HYD_STEP);

		for (int j = 0; j < network.count(Element::LINK); j++)
		{
			Link* link = network.link(j);
			if (link->type() == Link::VALVE)
			{
				Valve* valve = static_cast<Valve*>(link);
				if (valve->valveType == Valve::DPRV)
				{
					if (t == 0)
					{
						valve->Xm = 0.2;
						valve->Xm_Last = 0.2;
						valve->delta_Xm = 0;
						valve->errorValve = 0;
						valve->errorSumValve = 0;
						valve->errorDifValve = 0;
						valve->errorPreValve = 0.5;
					}

					pToNode = valve->toNode->head - valve->toNode->elev;
					pToPastNode = valve->toNode->pastHead - valve->toNode->elev;
					pFromNode = valve->fromNode->head - valve->fromNode->elev;

					if (valve->presManagType == Valve::FO)
					{
						ref = valve->fixedOutletPressure / network.ucf(Units::PRESSURE);

						if (valve->status == Valve::LINK_CLOSED)
						{
							if (pFromNode > ref && pToNode < ref)
								valve->status = Valve::VALVE_ACTIVE;
						}
					}
						
					if (valve->status == Valve::VALVE_ACTIVE)
					{

						// Fixed Outlet Pressure Control.

						if (valve->presManagType == Valve::FO)
						{
							ref = valve->fixedOutletPressure / network.ucf(Units::PRESSURE);

							valve->errorValve = ref - pToNode;
						}

						// Time Modulated Pressure Control. Time Schedule can be adjusted arbitrarily.

						else if (valve->presManagType == Valve::TM) 
						{
							if (0 <= t && t <= 3600)
								ref = valve->dayPressure / network.ucf(Units::PRESSURE);
							else if (3600 <= t && t <= 18000)
								ref = valve->nightPressure / network.ucf(Units::PRESSURE);
							else if (18000 <= t && t <= 90000)
								ref = valve->dayPressure / network.ucf(Units::PRESSURE);
							else if (90000 <= t && t <= 104400)
								ref = valve->nightPressure / network.ucf(Units::PRESSURE);
							else if (104400 <= t && t <= 176400)
								ref = valve->dayPressure / network.ucf(Units::PRESSURE);
							else if (176400 <= t && t <= 190800)
								ref = valve->nightPressure / network.ucf(Units::PRESSURE);
							else if (190800 <= t && t <= 262800)
								ref = valve->dayPressure / network.ucf(Units::PRESSURE);
							else if (262800 <= t && t <= 277200)
								ref = valve->nightPressure / network.ucf(Units::PRESSURE);
							else if (277200 <= t && t <= 349200)
								ref = valve->dayPressure / network.ucf(Units::PRESSURE);
							else if (349200 <= t && t <= 363600)
								ref = valve->nightPressure / network.ucf(Units::PRESSURE);
							else if (363600 <= t && t <= 435600)
								ref = valve->dayPressure / network.ucf(Units::PRESSURE);
							else if (435600 <= t && t <= 450000)
								ref = valve->nightPressure / network.ucf(Units::PRESSURE);
							else if (450000 <= t && t <= 522000)
								ref = valve->dayPressure / network.ucf(Units::PRESSURE);
							else if (522000 <= t && t <= 536400)
								ref = valve->nightPressure / network.ucf(Units::PRESSURE);
							else if (536400 <= t && t <= 604800)
								ref = valve->dayPressure / network.ucf(Units::PRESSURE); // */

							valve->errorValve = ref - pToNode;
						}

						// Flow Modulated Pressure Control

						else if (valve->presManagType == Valve::FM)
						{
							ref = (valve->a_FM * (valve->flow * network.ucf(Units::FLOW))* (valve->flow * network.ucf(Units::FLOW)) + valve->b_FM * (valve->flow * network.ucf(Units::FLOW)) + valve->c_FM) / network.ucf(Units::LENGTH);

							valve->errorValve = ref - pToNode;
						}

						// Remote Node Modulated Pressure Control

						else if (valve->presManagType == Valve::RNM)
						{
							pRemoteNode = valve->remoteNode->head - valve->remoteNode->elev;
							ref = valve->rnmPressure / network.ucf(Units::PRESSURE);
							valve->errorValve = ref - pRemoteNode;
						}

						// PRV Parameters

						double Vcontrol = 0.0047;
						double lift = 0.057;
						double k5 = 1.30;
						double k6 = 0.56;
						double Acs = (k5 * valve->Xm * valve->Xm + k6) * Vcontrol / lift; // m2
	
						double q3 = 0;

						// Physical Based Control
						if (valve->errorValve >= 0)
						{
							q3 = alfaopen * valve->errorValve;
						}
						else if (valve->errorValve <= 0)
						{
							q3 = alfaclose * valve->errorValve;
						}

						valve->delta_Xm = (q3 / Acs) * deltat; // */

						// PID Control

						/*valve->errorSumValve = valve->errorSumValve + (valve->errorValve); // / 12);
						if (valve->errorSumValve <= -100)
							valve->errorSumValve = -100;
						else if (valve->errorSumValve > 100)
							valve->errorSumValve = 100;

						valve->errorDifValve = (valve->errorValve - valve->errorPreValve); // *12;

						double pInput = Kp * valve->errorValve;
						double iInput = Ki * valve->errorSumValve;
						double dInput = Kd * (pToNode - pToPastNode); // valve->errorDifValve;

						q3 = -(pInput + iInput + dInput);

						valve->delta_Xm = (q3 / Acs) * deltat; // */
		
						valve->Xm = valve->delta_Xm + valve->Xm_Last;
					}

					if (valve->Xm < 0)
						valve->Xm = 0;
					if (valve->Xm > 1)
						valve->Xm = 1;
					else
						valve->Xm = valve->Xm;

					outFile << Utilities::getTime(t) << " " << valve->Xm << "\n";
				}
				else
					continue;
			}
			else
				continue;
		}
	}


	double Project::computeWaterLoss(double totalLoss)
	{
		totalLeak = 0;
		int nodeCount = network.count(Element::NODE);
		int linkCount = network.count(Element::LINK);

		for (int j = 0; j < linkCount; j++)
		{
			totalLeak += network.link(j)->leakage * network.ucf(Units::FLOW);
		}

		totalLoss = totalLeak; // */

		return totalLoss; 
	}
	
	void Project::lasting()
	{
		int linkCount = network.count(Element::LINK);

		for (int j = 0; j < linkCount; j++)
		{
			Link* link = network.link(j);
			if (link->type() == Link::VALVE)
			{
				Valve* valve = static_cast<Valve*>(link);
				if (valve->valveType == Valve::DPRV)
				{
					valve->Xm_Last = valve->Xm;
					valve->errorPreValve = valve->errorValve;
				}
				else
					continue;
			}
			else
				continue;
		}
	}

}

	