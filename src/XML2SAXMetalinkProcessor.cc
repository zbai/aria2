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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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
#include "XML2SAXMetalinkProcessor.h"

#include <cassert>

#include "BinaryStream.h"
#include "MetalinkParserStateMachine.h"
#include "Metalinker.h"
#include "MetalinkEntry.h"
#include "util.h"
#include "message.h"
#include "DlAbortEx.h"
#include "A2STR.h"

namespace aria2 {

class SessionData {
public:
  SharedHandle<MetalinkParserStateMachine> _stm;

  std::deque<std::string> _charactersStack;

  SessionData(const SharedHandle<MetalinkParserStateMachine>& stm):_stm(stm) {}
};

static void mlStartElement
(void* userData,
 const xmlChar* srcLocalname,
 const xmlChar* srcPrefix,
 const xmlChar* srcNsUri,
 int numNamespaces,
 const xmlChar **namespaces,
 int numAttrs,
 int numDefaulted,
 const xmlChar **attrs)
{
  SessionData* sd = reinterpret_cast<SessionData*>(userData);
  std::vector<XmlAttr> xmlAttrs;
  size_t index = 0;
  for(int attrIndex = 0; attrIndex < numAttrs; ++attrIndex, index += 5) {
    XmlAttr xmlAttr;
    assert(attrs[index]);
    xmlAttr.localname = reinterpret_cast<const char*>(attrs[index]);
    if(attrs[index+1]) {
      xmlAttr.prefix = reinterpret_cast<const char*>(attrs[index+1]);
    }
    if(attrs[index+2]) {
      xmlAttr.nsUri = reinterpret_cast<const char*>(attrs[index+2]);
    }
    const char* valueBegin = reinterpret_cast<const char*>(attrs[index+3]);
    const char* valueEnd = reinterpret_cast<const char*>(attrs[index+4]);
    xmlAttr.value = std::string(valueBegin, valueEnd);
    xmlAttrs.push_back(xmlAttr);
  }
  assert(srcLocalname);
  std::string localname = reinterpret_cast<const char*>(srcLocalname);
  std::string prefix;
  std::string nsUri;
  if(srcPrefix) {
    prefix = reinterpret_cast<const char*>(srcPrefix);
  }
  if(srcNsUri) {
    nsUri = reinterpret_cast<const char*>(srcNsUri);
  }
  sd->_stm->beginElement(localname, prefix, nsUri, xmlAttrs);
  if(sd->_stm->needsCharactersBuffering()) {
    sd->_charactersStack.push_front(A2STR::NIL);
  }
}

static void mlEndElement
(void* userData,
 const xmlChar* srcLocalname,
 const xmlChar* srcPrefix,
 const xmlChar* srcNsUri)
{
  SessionData* sd = reinterpret_cast<SessionData*>(userData);
  std::string characters;
  if(sd->_stm->needsCharactersBuffering()) {
    characters = sd->_charactersStack.front();
    sd->_charactersStack.pop_front();
  }
  std::string localname = reinterpret_cast<const char*>(srcLocalname);
  std::string prefix;
  std::string nsUri;
  if(srcPrefix) {
    prefix = reinterpret_cast<const char*>(srcPrefix);
  }
  if(srcNsUri) {
    nsUri = reinterpret_cast<const char*>(srcNsUri);
  }
  sd->_stm->endElement(localname, prefix, nsUri, characters);
}

static void mlCharacters(void* userData, const xmlChar* ch, int len)
{
  SessionData* sd = reinterpret_cast<SessionData*>(userData);
  if(sd->_stm->needsCharactersBuffering()) {
    sd->_charactersStack.front() += std::string(&ch[0], &ch[len]);
  }
}

static xmlSAXHandler mySAXHandler =
  {
    0, // internalSubsetSAXFunc
    0, // isStandaloneSAXFunc
    0, // hasInternalSubsetSAXFunc
    0, // hasExternalSubsetSAXFunc
    0, // resolveEntitySAXFunc
    0, // getEntitySAXFunc
    0, // entityDeclSAXFunc
    0, // notationDeclSAXFunc
    0, // attributeDeclSAXFunc
    0, // elementDeclSAXFunc
    0, //   unparsedEntityDeclSAXFunc
    0, //   setDocumentLocatorSAXFunc
    0, //   startDocumentSAXFunc
    0, //   endDocumentSAXFunc
    0, //   startElementSAXFunc
    0, //   endElementSAXFunc
    0, //   referenceSAXFunc
    &mlCharacters, //   charactersSAXFunc
    0, //   ignorableWhitespaceSAXFunc
    0, //   processingInstructionSAXFunc
    0, //   commentSAXFunc
    0, //   warningSAXFunc
    0, //   errorSAXFunc
    0, //   fatalErrorSAXFunc
    0, //   getParameterEntitySAXFunc
    0, //   cdataBlockSAXFunc
    0, //   externalSubsetSAXFunc
    XML_SAX2_MAGIC, //   unsigned int        initialized
    0, //   void *      _private
    &mlStartElement, //   startElementNsSAX2Func
    &mlEndElement, //   endElementNsSAX2Func
    0, //   xmlStructuredErrorFunc
  };

SharedHandle<Metalinker>
MetalinkProcessor::parseFile(const std::string& filename)
{
  _stm.reset(new MetalinkParserStateMachine());
  SharedHandle<SessionData> sessionData(new SessionData(_stm));
  int retval = xmlSAXUserParseFile(&mySAXHandler, sessionData.get(),
                                   filename.c_str());
  if(retval != 0) {
    throw DL_ABORT_EX(MSG_CANNOT_PARSE_METALINK);
  }
  return _stm->getResult();
}
         
SharedHandle<Metalinker>
MetalinkProcessor::parseFromBinaryStream(const SharedHandle<BinaryStream>& binaryStream)
{
  _stm.reset(new MetalinkParserStateMachine());
  size_t bufSize = 4096;
  unsigned char buf[bufSize];

  ssize_t res = binaryStream->readData(buf, 4, 0);
  if(res != 4) {
    throw DL_ABORT_EX("Too small data for parsing XML.");
  }

  SharedHandle<SessionData> sessionData(new SessionData(_stm));
  xmlParserCtxtPtr ctx = xmlCreatePushParserCtxt
    (&mySAXHandler, sessionData.get(),
     reinterpret_cast<const char*>(buf), res, 0);
  try {
    off_t readOffset = res;
    while(1) {
      ssize_t res = binaryStream->readData(buf, bufSize, readOffset);
      if(res == 0) {
        break;
      }
      if(xmlParseChunk(ctx, reinterpret_cast<const char*>(buf), res, 0) != 0) {
        throw DL_ABORT_EX(MSG_CANNOT_PARSE_METALINK);
      }
      readOffset += res;
    }
    xmlParseChunk(ctx, reinterpret_cast<const char*>(buf), 0, 1);
  } catch(Exception& e) {
    xmlFreeParserCtxt(ctx);
    throw;
  }
  xmlFreeParserCtxt(ctx);

  if(!_stm->finished()) {
    throw DL_ABORT_EX(MSG_CANNOT_PARSE_METALINK);
  }
  return _stm->getResult();
}

} // namespace aria2
