/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2006 Tatsuhiro Tsujikawa
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#include "MultiUrlRequestInfo.h"
#include "RequestGroupMan.h"
#include "DownloadEngine.h"
#include "LogFactory.h"
#include "RequestGroup.h"
#include "prefs.h"
#include "DownloadEngineFactory.h"
#include "RecoverableException.h"
#include "message.h"
#include "DNSCache.h"
#include "Util.h"
#include "ConsoleStatCalc.h"
#include <signal.h>

#ifndef SA_RESETHAND
# define SA_RESETHAND 0x80000000
#endif // SA_RESETHAND

extern volatile sig_atomic_t globalHaltRequested;

static void handler(int signal) {
  globalHaltRequested = true;
}

MultiUrlRequestInfo::MultiUrlRequestInfo(const RequestGroups& requestGroups, Option* op):
  _requestGroups(requestGroups),
  _option(op),
  _logger(LogFactory::getInstance())
{}

MultiUrlRequestInfo::~MultiUrlRequestInfo() {}

void MultiUrlRequestInfo::printMessageForContinue()
{
  cout << "\n"
       << _("aria2 will resume download if the transfer is restarted.")
       << "\n"
       << _("If there are any errors, then see the log file. See '-l' option in help/man page for details.")
       << "\n";
}

void MultiUrlRequestInfo::execute()
{
  {
    DNSCacheHandle dnsCache = new SimpleDNSCache();
    DNSCacheSingletonHolder::instance(dnsCache);
  }

  try {
    DownloadEngineHandle e =
      DownloadEngineFactory().newDownloadEngine(_option, _requestGroups);
    e->setStatCalc(new ConsoleStatCalc());

    e->fillCommand();

    // The number of simultaneous download is specified by PREF_MAX_CONCURRENT_DOWNLOADS.
    // The remaining urls are queued into FillRequestGroupCommand.
    // It observes the number of simultaneous downloads and if it is under
    // the limit, it adds RequestGroup object from its queue to DownloadEngine.
    // This is done every 1 second. At the same time, it removes finished/error
    // RequestGroup from DownloadEngine.

    Util::setGlobalSignalHandler(SIGINT, handler, SA_RESETHAND);
    Util::setGlobalSignalHandler(SIGTERM, handler, SA_RESETHAND);
    
    e->run();
    
    e->_requestGroupMan->showDownloadResults(cout);
    cout << flush;

    if(!e->_requestGroupMan->downloadFinished()) {
      printMessageForContinue();
    }
  } catch(RecoverableException *ex) {
    _logger->error(EX_EXCEPTION_CAUGHT, ex);
    delete ex;
  }
  Util::setGlobalSignalHandler(SIGINT, SIG_DFL, 0);
  Util::setGlobalSignalHandler(SIGTERM, SIG_DFL, 0);
}
