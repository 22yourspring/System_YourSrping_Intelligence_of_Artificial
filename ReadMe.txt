*******************************************************************************
        SYSIA  (System YourSrping & Intelligence of Artificial)
*******************************************************************************

Environment Setup :
- Visual Studio 2015
- Uses Windows environment variable BWAPI_DIR which points to BWAPI 4.1.2
  (ex : BWAPI_DIR = C:\StarCraft\bwlibrary\BWAPI412)
To Compile :
- Open src/SYSIA.sln in VS2015
- Select Release_Server_DLL mode
- Build the SYSIA project
- Output will go to src/Release_Server_DLL/SYSIA/SYSIA.dll

Tournament Setup :
- Copy SYSIA.dll to the tournament SYSIA/AI folder

References :
UAlberta Bot (Thanks to Author : David Churchill)
BWEM (Map Analyzer, Thanks to Author : Igor Dimitrijevic)

Brief Strategy : 
SYSIA is able to change battle strategies and various combinations of units dynamically 
according to the enemy strategy to get higher probabilities for winning the game. 
Also it has high performance in gathering resources. 
Finally it does powerful attack at appropriate time that is decided by our ML model.

###########################################################
SYSIA Designed by Yi seung hun from South Korea
Team Members : Changhyeon Bae(Leader), Daehun Jun, Iljoo Yoon, Junseung Lee, 
               Hyunjae Lee, Hyunjin Choi, Uk Jo, Yonghyun Jeong
Contact : skywooya@icloud.com
About YouTube Channel : www.youtube.com/22yourspring
