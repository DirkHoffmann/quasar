/* © Copyright CERN, Universidad de Oviedo, 2015.  All rights not expressly granted are reserved.
 * BaseQuasarServer.cpp
 *
 *  Created on: Nov 6, 2015
 * 		Author: Damian Abalo Miron <damian.abalo@cern.ch>
 *      Author: Piotr Nikiel <piotr@nikiel.info>
 *
 *  This file is part of Quasar.
 *
 *  Quasar is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public Licence as published by
 *  the Free Software Foundation, either version 3 of the Licence.
 *
 *  Quasar is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public Licence for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with Quasar.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <BaseQuasarServer.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <iostream>
#include "uaplatformlayer.h"
#include <boost/program_options.hpp>
#include "version.h"

#include <Configuration.hxx>
#include <Configurator.h>

#include <shutdown.h>
#include <LogIt.h>
#include <LogLevels.h>
#ifdef SUPPORT_XML_CONFIG
  #include "xmldocument.h"
#endif

#include <shutdown.h>
#include <SourceVariables.h>
#include <meta.h>
#include <boost/foreach.hpp>
#include <boost/bind.hpp>

using namespace std;
using namespace boost::program_options;


BaseQuasarServer::BaseQuasarServer():
	m_pServer(0), 
	m_nodeManager(0)
{	
}

BaseQuasarServer::~BaseQuasarServer()
{
    LOG(Log::TRC) << "Entered BaseQuasarServer dtr.";
    shutdownEnvironment();
}

int BaseQuasarServer::startApplication(int argc, char *argv[])
{
    RegisterSignalHandler();
	
    bool isHelpOrVersion = false;	
    string configurationFileName  = "config.xml";
    bool isCreateCertificateOnly = false;
	
    int ret = parseCommandLine(argc, argv, &isHelpOrVersion, &isCreateCertificateOnly, &configurationFileName);
	
    if(ret != 0 || isHelpOrVersion)//If there was a problem parsing the arguments, or it was a help/version call, we finish the execution
	return ret;
    try
    {
	ret = serverRun(configurationFileName, isCreateCertificateOnly);
	std::cout << "OpcServerMain() exited with code [" << ret << "]" << std::endl;
	return ret;
    }
    catch (std::runtime_error &e)
    {
	std::cerr << "Caught runtime exception with msg: [" << e.what() << "]" << std::endl;
	return 1;
    }
}

int BaseQuasarServer::serverRun(const std::string& configFileName, bool onlyCreateCertificate)
{
    const std::string serverSettingsPath = getApplicationPath();
    const int initializeEnvironmentReturn = initializeEnvironment();
    if(initializeEnvironmentReturn != 0)
    {
	LOG(Log::ERR) << "Initialization of components failed. Return code: [" << initializeEnvironmentReturn << "]";
	return initializeEnvironmentReturn;
    }
	
    //- Start up OPC server ---------------------
    // This code can be integrated into a start up
    // sequence of the application where the
    // OPC server should be integrated
    //-------------------------------------------
    // Create and initialize server object
    m_pServer = new OpcServer;
    m_pServer->setServerConfig(getServerConfigFullPath(serverSettingsPath), serverSettingsPath.c_str());

    if (onlyCreateCertificate)
    {
	m_pServer->createCertificate();
	LOG(Log::INF) << "Create certificate only";
	return 0;
    }

    m_nodeManager = new AddressSpace::ASNodeManager();
    m_nodeManager->setAfterStartupDelegate( boost::bind(&BaseQuasarServer::configurationInitializerHandler, this, configFileName, m_nodeManager) );
    m_pServer->addNodeManager(m_nodeManager);

    try
    {
	const int startServerReturn = m_pServer->start();
	if ( startServerReturn != 0 )
	{
	    serverStartFailLogError(startServerReturn, m_pServer->getLogFilePath());

	    return startServerReturn;
	}
	mainLoop();
	
    }
    catch (const std::exception &e)
    {
	LOG(Log::ERR) << "Exception caught in BaseQuasarServer::serverRun:  [" << e.what() << "]";
    }

    shutdown();  // this is typically overridden by the developer

    unlinkAllDevices(m_nodeManager);
    destroyMeta(m_nodeManager);
    Device::DRoot::getInstance()->unlinkAllChildren();
    m_pServer->stop(3, UaLocalizedText("", "User shut down"));

    if (m_pServer != 0)
    {
	delete m_pServer;
	m_pServer = NULL;
    }

 
    return 0;
}

std::string BaseQuasarServer::getApplicationPath() const
{  
#ifdef __linux__
    char serverSettingsPath[PATH_MAX];
    memset( serverSettingsPath, 0, sizeof serverSettingsPath  );
    readlink("/proc/self/exe", serverSettingsPath, sizeof(serverSettingsPath));
    char *pszFind = strrchr(serverSettingsPath, '/');
#elif _WIN32
    char serverSettingsPath[MAX_PATH];
    memset( serverSettingsPath, 0, sizeof serverSettingsPath  );
    int bytes = GetModuleFileNameA(NULL, serverSettingsPath, MAX_PATH);
    char *pszFind = strrchr(serverSettingsPath, '\\');
#else
    char serverSettingsPath[0];//We declare this so the compiler is happy
#error "ERROR: Unable to determine your platform."
#endif
    if (pszFind)
    {
	*pszFind = 0; // cut off appname
    }

    return std::string(serverSettingsPath);
}

int BaseQuasarServer::parseCommandLine(int argc, char *argv[], bool *isHelpOrVersion, bool *isCreateCertificateOnly, std::string *configurationFileName)
{	
    bool createCertificateOnly = false;
    bool printVersion = false;
    string logFile;
    options_description desc ("Allowed options");

    desc.add_options()
	("config_file", value<string>(), "A path to the config file")
	("create_certificate", bool_switch(&createCertificateOnly), "Create new certificate and exit" )
	("help", "Print help")
	("version", bool_switch(&printVersion), "Print version and exit");

    positional_options_description p;
    p.add("config_file", 1);

    appendCustomCommandLineOptions(desc, p);

    variables_map vm;
    try
    {
	store(command_line_parser(argc,argv)
	      .options(desc)
	      .style(command_line_style::allow_long_disguise | command_line_style::unix_style)
	      .positional(p)
	      .run(),
	      vm);
    }
    catch (boost::exception &e)
    {
	cout << "Couldn't interpret command line, please run with -help "  << endl;
	return 1;
    }
    notify(vm);
    if (vm.count("help"))
    {
	cout << desc << endl;
	if (isHelpOrVersion)
	    *isHelpOrVersion = true;
	return 0;
    }
    //Print version if needed
    if (printVersion)
    {
	std::cout << VERSION_STR << std::endl;
	if (isHelpOrVersion)
	    *isHelpOrVersion = true;
	return 0;
    }
    else
    {		
	if (vm.count("config_file") > 0)
	    *configurationFileName = vm["config_file"].as< string > ();
	*isHelpOrVersion = false;
	*isCreateCertificateOnly = createCertificateOnly;
	return 0;
    }
}
int BaseQuasarServer::initializeEnvironment()
{
    //- initialize the environment --------------
#ifdef SUPPORT_XML_CONFIG
    // initialize the XML Parser
    UaXmlDocument::initParser();
#endif
    // initialize the UA Stack platform layer
    const int ret = UaPlatformLayer::init();
    //-------------------------------------------
    initializeLogIt();    
    return ret;
}
void BaseQuasarServer::initializeLogIt()
{
	Log::initializeLogging();
	LOG(Log::ERR) << "Testing logging ";
}
void BaseQuasarServer::mainLoop()
{
    printServerMsg("Press "+std::string(SHUTDOWN_SEQUENCE)+" to shutdown server");

    // Wait for user command to terminate the server thread.

    while(ShutDownFlag() == 0)
    {
	UaThread::sleep (1);
    }
    printServerMsg(" Shutting down server");
}
bool BaseQuasarServer::overridableConfigure(const std::string& fileName, AddressSpace::ASNodeManager *nm)
{
	return configure (fileName, nm);
}
UaString BaseQuasarServer::getServerConfigFullPath(const std::string& serverSettingsPath) const
{
    // Create configuration file name
    UaString sConfigFileName(serverSettingsPath.c_str());

#ifdef SUPPORT_XML_CONFIG
    sConfigFileName += "/ServerConfig.xml";
#else
    sConfigFileName += "/ServerConfig.ini";
#endif
    return sConfigFileName;
}

void BaseQuasarServer::shutdownEnvironment()
{
    //- Cleanup the environment --------------
    // Cleanup the UA Stack platform layer
    UaPlatformLayer::cleanup();
#ifdef SUPPORT_XML_CONFIG
    // Cleanup the XML Parser
    UaXmlDocument::cleanupParser();
#endif
    //-------------------------------------------
}

void BaseQuasarServer::serverStartFailLogError(int ret, const std::string& logFilePath)
{
    LOG(Log::ERR) << "Starting up of the server failed. Return code: [" << ret << "]";
    if (logFilePath != "")
	LOG(Log::ERR) << "The reason of failure should have been logged in your server log file: " << logFilePath;			
    else
    {
	LOG(Log::ERR) << "The exact reason is unknown because you haven't enabled logging in your ServerConfig file.";
	LOG(Log::ERR) << "To enable, change value of <UaAppTraceEnabled> content to true.";			
    }
}

void BaseQuasarServer::printServerMsg(const std::string& message)
{
    LOG(Log::INF) << "***************************************************";
    LOG(Log::INF) << message;
    LOG(Log::INF) << "***************************************************";
}

UaStatus BaseQuasarServer::configurationInitializerHandler(const std::string& configFileName, AddressSpace::ASNodeManager *nm)
{
    LOG(Log::INF) << "Configuration Initializer Handler";
    if (!overridableConfigure (configFileName, nm))
	return OpcUa_Bad; // error is already printed in configure()
    validateDeviceTree();
    
    initialize();
    return OpcUa_Good;
}

void BaseQuasarServer::appendCustomCommandLineOptions(options_description& commandLineOptions, positional_options_description& positionalOptionsDescription)
{}
