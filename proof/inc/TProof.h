// @(#)root/proof:$Name:  $:$Id: TProof.h,v 1.46 2004/10/13 15:34:18 rdm Exp $
// Author: Fons Rademakers   13/02/97

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TProof
#define ROOT_TProof


//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TProof                                                               //
//                                                                      //
// This class controls a Parallel ROOT Facility, PROOF, cluster.        //
// It fires the slave servers, it keeps track of how many slaves are    //
// running, it keeps track of the slaves running status, it broadcasts  //
// messages to all slaves, it collects results, etc.                    //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#ifndef ROOT_TVirtualProof
#include "TVirtualProof.h"
#endif
#ifndef ROOT_TProofDebug
#include "TProofDebug.h"
#endif
#ifndef ROOT_TString
#include "TString.h"
#endif
#ifndef ROOT_MessageTypes
#include "MessageTypes.h"
#endif
#ifndef ROOT_TMD5
#include "TMD5.h"
#endif
#ifndef ROOT_TSocket
#include "TSocket.h"
#endif

#include <map>

#ifdef R__GLOBALSTL
namespace std { using ::map; }
#endif


class TMessage;
class TMonitor;
class TSignalHandler;
class TPluginHandler;
class TSlave;
class TProofServ;
class TProofInputHandler;
class TProofInterruptHandler;
class TProofPlayer;
class TProofPlayerRemote;
class TPacketizer2;
class TCondor;

// protocol changes:
// 1 -> 2: new arguments for Process() command, option added
// 2 -> 3: package manager enabling protocol changed

// PROOF magic constants
const Int_t       kPROOF_Protocol = 3;             // protocol version number
const Int_t       kPROOF_Port     = 1093;          // IANA registered PROOF port
const char* const kPROOF_ConfFile = "proof.conf";  // default config file
const char* const kPROOF_ConfDir  = "/usr/local/root";  // default config dir
const char* const kPROOF_WorkDir  = "~/proof";     // default working directory
const char* const kPROOF_CacheDir = "cache";       // file cache dir, under WorkDir
const char* const kPROOF_PackDir  = "packages";    // package dir, under WorkDir
const char* const kPROOF_CacheLockFile   = "/tmp/proof-cache-lock-";   // cache lock file
const char* const kPROOF_PackageLockFile = "/tmp/proof-package-lock-"; // package lock file


class TSlaveInfo : public TObject {
public:
   enum ESlaveStatus { kActive, kNotActive, kBad };

   Int_t        fOrdinal;      //slave ordinal
   TString      fHostName;     //hostname this slave is running on
   Int_t        fPerfIndex;    //relative performance of this slave
   ESlaveStatus fStatus;       //slave status

   TSlaveInfo(Int_t ordinal = 0, const char *host = "", Int_t perfidx = 0)
      : fOrdinal(ordinal), fHostName(host), fPerfIndex(perfidx),
        fStatus(kNotActive) { }

   const char *GetName() const { return fHostName; }
   Int_t       GetOrdinal() const { return fOrdinal; }
   void        SetStatus(ESlaveStatus stat) { fStatus = stat; }

   void Print(Option_t *option="") const;

   ClassDef(TSlaveInfo,1) //basic info on slave
};


class TProof : public TVirtualProof {

friend class TProofServ;
friend class TProofInputHandler;
friend class TProofInterruptHandler;
friend class TProofPlayer;
friend class TProofPlayerRemote;
friend class TSlave;
friend class TPacketizer;
friend class TPacketizer2;
friend class TCondor;

private:
   Bool_t          fValid;          //is this a valid proof object
   TString         fMaster;         //name of master server (use "" if this is a master)
   TString         fConfDir;        //directory containing cluster config information
   TString         fConfFile;       //file containing config information
   TString         fWorkDir;        //current work directory on remote servers
   TString         fUser;           //user under which to run
   TString         fImage;          //master's image name
   TString         fUrlProtocol;    //net protocol name
   Int_t           fPort;           //port we are connected to (proofd = 1093)
   TSecContext    *fSecContext;     //SecContext of the related authentication
   Int_t           fProtocol;       //remote PROOF server protocol version number
   Int_t           fLogLevel;       //server debug logging level
   Int_t           fStatus;         //remote return status (part of kPROOF_LOGDONE)
   Int_t           fParallel;       //number of active slaves (only set on client, on server use fActiveSlaves)
   TList          *fSlaveInfo;      //!list returned by kPROOF_GETSLAVEINFO
   Bool_t          fMasterServ;     //true if we are a master server
   Bool_t          fSendGroupView;  //if true send new group view
   TList          *fSlaves;         //list of all slave servers as in config file
   TList          *fActiveSlaves;   //list of active slaves (subset of all slaves)
   TList          *fUniqueSlaves;   //list of all active slaves with unique file systems
   TList          *fBadSlaves;      //dead slaves (subset of all slaves)
   TMonitor       *fAllMonitor;     //monitor activity on all valid slave sockets
   TMonitor       *fActiveMonitor;  //monitor activity on all active slave sockets
   TMonitor       *fUniqueMonitor;  //monitor activity on all unique slave sockets
   Double_t        fBytesRead;      //bytes read by all slaves during the session
   Float_t         fRealTime;       //realtime spent by all slaves during the session
   Float_t         fCpuTime;        //CPU time spent by all slaves during the session
   TSignalHandler *fIntHandler;     //interrupt signal handler (ctrl-c)
   TPluginHandler *fProgressDialog; //progress dialog plugin
   TProofPlayer   *fPlayer;         //current player
   TCondor  *fCondor;               //proxy for our Condor pool
   struct MD5Mod_t {
      TMD5   fMD5;              //file's md5
      Long_t fModtime;          //file's modification time
   };
   typedef std::map<TString, MD5Mod_t> FileMap_t;
   FileMap_t  fFileMap;         //map keeping track of a file's md5 and mod time

   enum ESlaves { kAll, kActive, kUnique };
   enum EUrgent { kHardInterrupt = 1, kSoftInterrupt, kShutdownInterrupt };

   TProof() { fSlaves = fActiveSlaves = fBadSlaves = 0; fCondor = 0; }
   TProof(const TProof &);           // not implemented
   void operator=(const TProof &);   // idem

   Int_t    Init(const char *masterurl, const char *conffile,
                 const char *confdir, Int_t loglevel);

   Int_t    Exec(const char *cmd, ESlaves list);
   Int_t    SendCommand(const char *cmd, ESlaves list = kActive);
   Int_t    SendCurrentState(ESlaves list = kActive);
   Long_t   CheckFile(const char *file, TSlave *sl);
   Int_t    SendFile(const char *file, Bool_t bin = kTRUE);
   Int_t    SendObject(const TObject *obj, ESlaves list = kActive);
   Int_t    SendGroupView();
   Int_t    SendInitialState();
   Int_t    SendPrint(Option_t *option="");
   Int_t    Ping(ESlaves list);
   void     Interrupt(EUrgent type, ESlaves list = kActive);
   void     AskStatus();
   Int_t    GoParallel(Int_t nodes);
   void     RecvLogFile(TSocket *s, Int_t size);
   Int_t    BuildPackage(const char *package);
   Int_t    LoadPackage(const char *package);

   Int_t    Broadcast(const TMessage &mess, TList *slaves);
   Int_t    Broadcast(const TMessage &mess, ESlaves list = kActive);
   Int_t    Broadcast(const char *mess, Int_t kind, TList *slaves);
   Int_t    Broadcast(const char *mess, Int_t kind = kMESS_STRING, ESlaves list = kActive);
   Int_t    Broadcast(Int_t kind, TList *slaves) { return Broadcast(0, kind, slaves); }
   Int_t    Broadcast(Int_t kind, ESlaves list = kActive) { return Broadcast(0, kind, list); }
   Int_t    BroadcastObject(const TObject *obj, Int_t kind, TList *slaves);
   Int_t    BroadcastObject(const TObject *obj, Int_t kind = kMESS_OBJECT, ESlaves list = kActive);
   Int_t    BroadcastRaw(const void *buffer, Int_t length, TList *slaves);
   Int_t    BroadcastRaw(const void *buffer, Int_t length, ESlaves list = kActive);
   Int_t    Collect(TList *slaves);
   Int_t    Collect(ESlaves list = kActive);
   Int_t    Collect(const TSlave *sl);
   Int_t    Collect(TMonitor *mon);

   void     FindUniqueSlaves();
   TSlave  *FindSlave(TSocket *s) const;
   TList   *GetListOfSlaves() const { return fSlaves; }
   TList   *GetListOfActiveSlaves() const { return fActiveSlaves; }
   TList   *GetListOfUniqueSlaves() const { return fUniqueSlaves; }
   TList   *GetListOfBadSlaves() const { return fBadSlaves; }
   Int_t    GetNumberOfSlaves() const;
   Int_t    GetNumberOfActiveSlaves() const;
   Int_t    GetNumberOfUniqueSlaves() const;
   Int_t    GetNumberOfBadSlaves() const;
   void     MarkBad(TSlave *sl);
   void     MarkBad(TSocket *s);

   void     ActivateAsyncInput();
   void     DeActivateAsyncInput();
   void     HandleAsyncInput(TSocket *s);

   void           SetPlayer(TProofPlayer *player) { fPlayer = player; };
   TProofPlayer  *GetPlayer() const { return fPlayer; };

public:
   TProof(const char *masterurl, const char *conffile = kPROOF_ConfFile,
          const char *confdir = kPROOF_ConfDir, Int_t loglevel = 0);
   virtual ~TProof();

   Int_t       Ping();
   Int_t       Exec(const char *cmd);
   Int_t       Process(TDSet *set, const char *selector, Option_t *option = "",
                       Long64_t nentries = -1, Long64_t firstentry = 0,
                       TEventList *evl = 0);
   Int_t       DrawSelect(TDSet *set, const char *varexp, const char *selection,
                          Option_t *option = "", Long64_t nentries = -1,
                          Long64_t firstentry = 0);

   void        StopProcess(Bool_t abort);
   void        AddInput(TObject *obj);
   void        ClearInput();
   TObject    *GetOutput(const char *name);
   TList      *GetOutputList();

   Int_t       SetParallel(Int_t nodes = 9999);
   void        SetLogLevel(Int_t level, UInt_t mask = TProofDebug::kAll);

   void        Close(Option_t *option="");
   void        Print(Option_t *option="") const;

   void        ShowCache(Bool_t all = kFALSE);
   void        ClearCache();
   void        ShowPackages(Bool_t all = kFALSE);
   void        ShowEnabledPackages(Bool_t all = kFALSE);
   void        ClearPackages();
   void        ClearPackage(const char *package);
   Int_t       EnablePackage(const char *package);
   Int_t       UploadPackage(const char *par, Int_t parallel = 1);

   const char *GetMaster() const { return fMaster; }
   const char *GetConfDir() const { return fConfDir; }
   const char *GetConfFile() const { return fConfFile; }
   const char *GetUser() const { return fUser; }
   const char *GetWorkDir() const { return fWorkDir; }
   const char *GetImage() const { return fImage; }
   const char *GetUrlProtocol() const { return fUrlProtocol; }
   Int_t       GetPort() const { return fPort; }
   Int_t       GetSecurity() const { return fSecContext->GetMethod(); }
   Int_t       GetRemoteProtocol() const { return fProtocol; }
   Int_t       GetClientProtocol() const { return kPROOF_Protocol; }
   Int_t       GetStatus() const { return fStatus; }
   Int_t       GetLogLevel() const { return fLogLevel; }
   Int_t       GetParallel() const;
   TList      *GetSlaveInfo();

   Double_t    GetBytesRead() const { return fBytesRead; }
   Float_t     GetRealTime() const { return fRealTime; }
   Float_t     GetCpuTime() const { return fCpuTime; }

   Bool_t      IsMaster() const { return fMasterServ; }
   Bool_t      IsValid() const { return fValid; }
   Bool_t      IsParallel() const { return GetParallel() > 0 ? kTRUE : kFALSE; }

   void        SetActive(Bool_t active = kTRUE);

   void        Progress(Long64_t total, Long64_t processed); //*SIGNAL*
   void        Feedback(TList *objs); //*SIGNAL*

   ClassDef(TProof,0)  //PROOF control class
};

#endif
