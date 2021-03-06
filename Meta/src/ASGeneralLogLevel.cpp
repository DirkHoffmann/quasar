/* © Copyright CERN, 2015.  All rights not expressly granted are reserved.
 * ASGeneralLogLevel.cpp
 *
 *  Created on: Aug 18, 2015
 * 	Author: Benjamin Farnham <benjamin.farnham@cern.ch>
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
#include <iostream>




#include <ASGeneralLogLevel.h>


#include <DGeneralLogLevel.h>


namespace AddressSpace
{



/*ctr*/
ASGeneralLogLevel::ASGeneralLogLevel (
    UaNodeId parentNodeId,
    const UaNodeId& typeNodeId,
    ASNodeManager *nm,
	const std::string& logLevel)
:OpcUa::BaseObjectType (nm->makeChildNodeId(parentNodeId,"GeneralLogLevel"), "GeneralLogLevel", nm->getNameSpaceIndex(), nm),
 m_typeNodeId (typeNodeId),
 m_logLevel (new ASDelegatingVariable<ASGeneralLogLevel> (nm->makeChildNodeId(this->nodeId(),UaString("logLevel")), UaString("logLevel"), nm->getNameSpaceIndex(), UaVariant(logLevel.c_str()), OpcUa_AccessLevels_CurrentReadOrWrite , nm)),
 m_deviceLink (0)
{
    UaVariant v;
    v.setString (logLevel.c_str());

    m_logLevel->setValue(/*pSession*/0, UaDataValue(UaVariant( v ), OpcUa_Good, UaDateTime::now(), UaDateTime::now() ), /*check access level*/OpcUa_False);

    const UaStatus s = nm->addNodeAndReference(this, m_logLevel, OpcUaId_HasComponent);
    if (!s.isGood())
    {
        std::cout << "While addNodeAndReference from " << this->nodeId().toString().toUtf8() << " to " << m_logLevel->nodeId().toString().toUtf8() << " : " << std::endl;
        ASSERT_GOOD(s);
    }

    m_logLevel->assignHandler(this, &ASGeneralLogLevel::writeLogLevel);
}

ASGeneralLogLevel::~ASGeneralLogLevel ()
{
    if (m_deviceLink != 0)
    {
        PRINT("deviceLink not zero!!");
        PRINT_LOCATION();
    }
}

UaStatus ASGeneralLogLevel::setLogLevel (const UaString & value, OpcUa_StatusCode statusCode,const UaDateTime & srcTime )
{
    UaVariant v;

    v.setString( value );

    return m_logLevel->setValue (0, UaDataValue (v, statusCode, srcTime, UaDateTime::now()), /*check access*/OpcUa_False  ) ;
}

UaStatus ASGeneralLogLevel::getLogLevel (UaString & r) const
{
    UaVariant v (* (m_logLevel->value(/*session*/0).value()));

    if (v.type() == OpcUaType_String)
    {
        r = v.toString();
        return OpcUa_Good;
    }
    else
        return OpcUa_Bad;
}


UaString ASGeneralLogLevel::getLogLevel () const
{
    UaVariant v (* m_logLevel->value (0).value() );
    UaString v_value;
    v_value = v.toString();
    return v_value;
}

UaStatus ASGeneralLogLevel::writeLogLevel (Session* pSession, const UaDataValue& dataValue, OpcUa_Boolean checkAccessLevel)
{
    /* ensure that data type passed by OPC UA client matches specification */
    UaVariant v (*dataValue.value());
    if (v.type() != OpcUaType_String )
    {
        return OpcUa_BadDataEncodingInvalid;

        if (v.type() != OpcUaType_Null)
            return OpcUa_BadDataEncodingInvalid; // now we know it is neither the intended datatype nor NULL
    }

    const UaString v_value(v.toString());

    if (m_deviceLink != 0)
    {
        return m_deviceLink->writeLogLevel(v_value);
    }
    else
        return OpcUa_Bad;
}

void ASGeneralLogLevel::linkDevice( Device::DGeneralLogLevel *deviceLink)
{
    if (m_deviceLink != 0)
    {
        /* This is an error -- device can be linked at most in the object's lifetime. */
        //TODO After error handling is discussed, abort in smarter way
        abort();

    }
    else
        m_deviceLink = deviceLink;
}


void ASGeneralLogLevel::unlinkDevice ()
{
    m_deviceLink = 0;
}


}

