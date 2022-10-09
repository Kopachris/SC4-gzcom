#include "include/cIGZApp.h"
#include "include/cIGZCheatCodeManager.h"
#include "include/cIGZFrameWork.h"
#include "include/cRZCOMDllDirector.h"
#include "include/cIGZMessage2.h"
#include "include/cIGZMessageServer2.h"
#include "include/cIGZMessage2Standard.h"
#include "include/cISC4AdvisorSystem.h"
#include "include/cISC4App.h"
#include "include/cISC4BuildingDevelopmentSimulator.h"
#include "include/cISC4City.h"
#include "include/cISC4Lot.h"
#include "include/cISC4LotConfiguration.h"
#include "include/cISC4LotConfigurationManager.h"
#include "include/cISC4LotDeveloper.h"
#include "include/cISC4LotManager.h"
#include "include/cISC4Occupant.h"
#include "include/cISC4OccupantManager.h"
#include "include/cISC4BuildingOccupant.h"
#include "include/cISC4RegionalCity.h"
#include "include/cISCPropertyHolder.h"
#include "include/cRZMessage2COMDirector.h"
#include "include/cRZMessage2Standard.h"
#include "include/cRZBaseString.h"
#include "include/GZServPtrs.h"
#include "include/SC4Rect.h"
#include <list>
#include <string>
#include <iostream>
#include <fstream>
#include <format>

#ifdef WIN32
#include <Windows.h>
#include "SG_InputBox.h"
#define DBGMSG(x) MessageBoxA(NULL, x, "Plugin Debug", MB_OK)
#define DBGINPUT(x) SG_InputBox::GetString("Plugin Debug", x)
#else
#define DBGMSG(x)
#define DBGINPUT(x)
#endif

static const uint32_t kGZIID_cIGZCheatCodeManager = 0xa1085722;
static const uint32_t kGZIID_cISC4App = 0x26ce01c0;
static const uint32_t kGZIID_cISC4Occupant = 649819774;
static const uint32_t kGZIID_cISC4BuildingOccupant = 0x87dda3a9;
static const uint32_t kGZIID_cISCExemplarPropertyHolder = 180532727;
static const uint32_t kGZIID_cISCPropertyHolder = 622944899;

static const uint32_t kGZMSG_CheatIssued = 0x230e27ac;
static const uint32_t kGZMSG_CityInited = 0x26d31ec1;
static const uint32_t kGZMSG_MonthPassed = 0x66956816;
static const uint32_t kGZMSG_OccupantInserted = 0x99ef1142;
static const uint32_t kGZMSG_OccupantRemoved = 0x99ef1143;
static const uint32_t kSC4MessageInsertBuildingOccupant = 0x8B8B2CED;

static const uint32_t kSCPROP_ExemplarID = 0x21;
static const uint32_t kSCPROP_CityExclusionGroup = 0xea2e078b;
static const uint32_t kOccupantType_Building = 0x278128a0;
static const uint32_t kOccupantType_Default = 0x74758925;
static const uint32_t kSC4CLSID_cSC4BuildingOccupant = 0xA9BD882D;

static const uint32_t kKCLotNamerPluginCOMDirectorID = 0x44d0baa0;

static const uint32_t kKCCheatRandomizeNames = 0x44ccbaa0;
static const char* kszKCCheatRandomizeNames = "NameGame";

static const char* dbgOccupantLog = "C:\\occupants.log";
constexpr auto hexfmt = "{:#010x}";

class kcGZLotNamerPluginCOMDirector : public cRZMessage2COMDirector
{
public:

	kcGZLotNamerPluginCOMDirector() {
		// Initialize variables here.
		// We can create objects using this COM director by satisfying
		// requests to GZCOM::GetClassObject if the class ID matches
		// our own.
	}

	virtual ~kcGZLotNamerPluginCOMDirector() {
		// You could do something here, but everything except the OS is
		// dead by the time you get here.
	}

	bool QueryInterface(uint32_t riid, void** ppvObj) {
		if (riid == GZCLSID::kcIGZMessageTarget2) {
			*ppvObj = static_cast<cIGZMessageTarget2*>(this);
			AddRef();
			return true;
		}
		else {
			return cRZCOMDllDirector::QueryInterface(riid, ppvObj);
		}
	}

	uint32_t AddRef(void) {
		return cRZCOMDllDirector::AddRef();
	}

	uint32_t Release(void) {
		return cRZCOMDllDirector::Release();
	}

	uint32_t GetDirectorID() const {
		return kKCLotNamerPluginCOMDirectorID;
	}

	bool OnStart(cIGZCOM* pCOM) {
		// This automatically gets called by the framework after loading
		// the library.
		//
		// Register for callbacks so we can get PreFrameWorkInit,
		// PreAppInit, and other callbacks.
		cIGZFrameWork* const pFramework = RZGetFrameWork();
		if (pFramework) {
			if (pFramework->GetState() < cIGZFrameWork::kStatePreAppInit) {
				pFramework->AddHook(static_cast<cIGZFrameWorkHooks*>(this));
			}
			else {
				PreAppInit();
			}
		}

		return true;
	}

	bool PreFrameWorkInit(void) { return true; }
	bool PreAppInit(void) { return true; }

	bool PostAppInit(void) {
		cIGZFrameWork* const pFramework = RZGetFrameWork();
		if (!pFramework)
			return false;

		cIGZApp* const pApp = pFramework->Application();
		if (!pApp)
			return false;

		cISC4App* pISC4App;
		if (!pApp->QueryInterface(kGZIID_cISC4App, (void**)&pISC4App))
			return false;

		cIGZCheatCodeManager* pCheatMgr = pISC4App->GetCheatCodeManager();
		if (pCheatMgr && pCheatMgr->QueryInterface(kGZIID_cIGZCheatCodeManager, (void**)&pCheatMgr)) {
			RegisterCheats(pCheatMgr);
		}
		cIGZMessageServer2Ptr pMsgServ;
		if (pMsgServ) {
			//pMsgServ->AddNotification(this, kGZMSG_CityInited);
			//pMsgServ->AddNotification(this, kGZMSG_MonthPassed);
			pMsgServ->AddNotification(this, kSC4MessageInsertBuildingOccupant);
			//pMsgServ->AddNotification(this, kGZMSG_OccupantRemoved);
		}
		return true;
	}

	bool PreAppShutdown(void) { return true; }
	bool PostAppShutdown(void) { return true; }
	bool PostSystemServiceShutdown(void) { return true; }
	bool AbortiveQuit(void) { return true; }
	bool OnInstall(void) { return true; }

	bool DoMessage(cIGZMessage2* pMessage) {
		cIGZMessage2Standard* pStandardMsg = static_cast<cIGZMessage2Standard*>(pMessage);
		uint32_t dwType = pMessage->GetType();
		const std::string szMsgType = std::format(hexfmt, dwType);

		//DBGMSG(szMsgType.c_str());

		if (dwType == kGZMSG_CheatIssued)
		{
			DBGMSG("Cheat issued.");
			uint32_t dwCheatID = pStandardMsg->GetData1();
			cIGZString* pszCheatData = static_cast<cIGZString*>(pStandardMsg->GetVoid2());
			const std::string szCheatID = std::format(hexfmt, dwCheatID);
			DBGMSG(szCheatID.c_str());
			DBGMSG(pszCheatData->ToChar());

			// if (dwCheatID == kKCCheatRandomizeNames) RandomizeAll();
		}
		else if (dwType == kSC4MessageInsertBuildingOccupant)
		{
			//DBGMSG("Occupant Inserted. Getting occupant.");
			cISC4Occupant* pOccupant = (cISC4Occupant*)pStandardMsg->GetVoid1();
			if (!pOccupant || pOccupant->GetType() != kOccupantType_Building) return true;
			
			cISC4BuildingOccupant* bOccupant;
			if (pOccupant->QueryInterface(kGZIID_cISC4BuildingOccupant, (void**)&bOccupant))
			{
				cRZBaseString thisOccupantName;
				bOccupant->GetName(thisOccupantName);
				std::string bldgName = "";
				if (thisOccupantName.Strlen() > 0)
					bldgName = "'" + (std::string)thisOccupantName.ToChar() + "'";
				else
					bldgName = "<Empty building name>";
				DBGMSG(bldgName.c_str());
			}
			else DBGMSG("Unable to cast.");

			//LogInsertOccupant(bOccupant);
			// RandomizeOne(pOccupant);
		}

		return true;
	}

protected:

	void RegisterCheats(cIGZCheatCodeManager* pCheatMgr) {
		// The message ID parameter has no effect, so just pass zero like
		// the rest of the game does.
		pCheatMgr->AddNotification2(this, 0);

		cRZBaseString szCheatName(kszKCCheatRandomizeNames);
		pCheatMgr->RegisterCheatCode(kKCCheatRandomizeNames, szCheatName);
	}

	void RandomizeAll() { }

	void RandomizeOne(cISC4Occupant* pOccupant) { }

	void LogBuildingOccupant(cISC4BuildingOccupant* thisBldgOccupant) 
	{
		DBGMSG("In LogBuildingOccupant()");
		// first get the building name
		cRZBaseString thisOccupantName;
		thisBldgOccupant->GetName(thisOccupantName);
		const char* bldgName = "";
		if (thisOccupantName.Strlen() > 0)
			const char* bldgName = thisOccupantName.ToChar();
		else
			const char* bldgName = "<Empty building name>";

		DBGMSG(bldgName);

		// let's try and output the hex building type too

		const std::string thisBldgTypeID = std::format(hexfmt, thisBldgOccupant->GetBuildingType());
		DBGMSG(thisBldgTypeID.c_str());

		std::ofstream txtOccupantLog;
		DBGMSG("Opening log file...");
		txtOccupantLog.open(dbgOccupantLog, std::ios::app);
		txtOccupantLog << thisBldgTypeID << " -- " << thisOccupantName.ToChar() << std::endl;
		txtOccupantLog.close();
		DBGMSG("Wrote to log.");
	}
};


cRZCOMDllDirector* RZGetCOMDllDirector() {
	static kcGZLotNamerPluginCOMDirector sDirector;
	return &sDirector;
}
