# EPANET_3-PMX
EPANET 3 Pressure Management Extension

This brief guideline is prepared for users to utilize EPANET 3 Pressure Management Extension (EPANET 3 PMX). The EPANET 3 PMX is developed for hydraulically modeling pressure management (PM) in water distribution systems (WDS) in a practical way. In order to use EPANET 3 PMX, the EPANET input file should contain at least one Dynamic Pressure Reducing Valve (DPRV). Unlike the conventional Pressure Reducing Valve (PRV) model in the current EPANET version, DPRV represents the physical behavior of the PRVs accurately (Koşucu & Demirel, 2022; Prescott & Ulanicki, 2003).

Please note that there are four PM methods in the literature whose names are 1. Fixed Outlet (FO) PM, 2. Time Modulated (TM) PM, 3. Flow Modulated (FM) PM, and 4. Remote Node Modulated (RNM) PM. Each method requires specific insertions and modifications in the EPANET input file. Firstly, The FO PM requires only a single outlet pressure value for PRV. Secondly, in the TM PM method, there are two outlet pressure values: day (between 05:00-01:00) and night (01:00-05:00). Thirdly, the FM PM is the PM method that modulates the PRV outlet pressure according to the flow rate of the WDS inlet. The modulation is realized through a second-order polynomial that is a*Q<sup>2</sup> + b*Q + c = PRV outlet pressure. Lastly, the RNM PM is implemented, when the outlet pressure of the PRV is modulated according to the remote node of the WDS. This method necessitates the remote node's target pressure and ID number, respectively.

80 EPANET input files belonging to hydraulic models of 18 different WDSs are provided with this extension. There is no PM implementation in 18 of 80 WDSs. FO PM, FM PM, and RNM PM methods are implemented in 18 WDSs each, and TM PM is implemented in 8 WDSs. It is understood from the file names which PM method is applied (i.e., EPA3-hk-large-peaked-high-FO.inp file contains the setting of FO PM).

## Usage
1. Modify the VALVES section of the EPANET 3 input (inp) files for PM methods:
- For Fixed Outlet PM, add:
	```
	[VALVES]
	;ID		Node1		Node2	Diameter	Type		PM_Type		Setting
	1		2010		1		400			DPRV		FO			42.5  ;
	```

- For Time Modulated PM, add:
	```
	[VALVES]
	;ID		Node1		Node2	Diameter	Type  PM_Type  Day_Pressure  Night_Pressure
	1		2010		1		400			DPRV	TM		42.5			25.5  ;
	```
- For Flow Modulated PM, add:
	```
	[VALVES]
	;ID		Node1		Node2	Diameter	Type  PM_Type		a_FM		b_FM	c_FM
	1		2010		1		400			DPRV	FM		0.00037110	0.00222011	25.17888967 ;
	```

- For Remote Node Modulated PM, add:
	```
	[VALVES]
	;ID		Node1		Node2	Diameter	Type	PM_Type		Remote_Node_Pressure	Remote_Node
	1		2010		1		400			DPRV	RNM				20						13150  ;
	```
	All the numbers above vary from one WDS to the other WDS. Implementing four PM methods depends on the valve settings on the EPANET input file.
2. Modify lines 74 and 76 in `Core/epanet3.cpp` if the result files are wished to be saved in another location. The default location is the path where the program was run. 
3. To run the command line executable under Linux/Mac enter the following command from a terminal window:

	```
	./run-epanet3 input.inp report.rpt

	```

	where `input.inp` is the name of a properly formatted EPANET input file and 	`report.rpt` is the name of a plain text file where results will be written. For Windows , enter the following command in a Command Prompt window:

	```
	run-epanet3 input.inp report.rpt

	```
	Alternatively, following command line could be written in Visual Studio. The path of the script is Debug => "Project" Properties => Configuration Properties => Debugging => Command Arguments:


	```
	input.inp report.rpt

	```

## Building
To build, use CMake on Windows with Visual Studio (tested with Visual Studio 2013 and validated with 2019):
```
mkdir build && cd build
cmake -G "Visual Studio n yyyy" ..
cmake --build . --config Release
```
## Contributors
Mehmet Melih Koşucu,		Istanbul Technical University
Mehmet Cüneyd Demirel,	Istanbul Technical University
Mustafa Alper Özdemir,		Istanbul Technical University

## References

Koşucu, M. M., & Demirel, M. C. (2022). Smart pressure management extension for EPANET: source code enhancement with a dynamic pressure reducing valve model. _Journal of Hydroinformatics_, _24_(3), 642–658. https://doi.org/10.2166/hydro.2022.172

Prescott, S. L., & Ulanicki, B. (2003). Dynamic Modeling of Pressure Reducing Valves. _Journal of Hydraulic Engineering_, _129_(10), 804–812. https://doi.org/10.1061/(ASCE)0733-9429(2003)129:10(804)
