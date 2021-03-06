/**************************************************************************************************
---------------------------------------------------------------------------------------------------
	Copyright (C) 2015  Jonathan Bagg
	This file is part of QtZeroConf.

	QtZeroConf is free software: you can redistribute it and/or modify
	it under the terms of the GNU Lesser General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	QtZeroConf is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public License
	along with QtZeroConf.  If not, see <http://www.gnu.org/licenses/>.
---------------------------------------------------------------------------------------------------
   Project name : QtZeroConf
   File name    : avahiclient.cpp
   Created      : 20 July 2015
   Author(s)    : Jonathan Bagg
---------------------------------------------------------------------------------------------------
   Avahi-client wrapper for use in Desktop Linux systems
---------------------------------------------------------------------------------------------------
**************************************************************************************************/
//#include <avahi-qt4/qt-watch.h>	//
#include "avahi-qt/qt-watch.h"		// fixme don't depend on avahi-qt until it supports Qt5
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/error.h>
#include <avahi-client/lookup.h>
#include "qzeroconf.h"

class QZeroConfPrivate
{
public:
	QZeroConfPrivate(QZeroConf *parent)
	{
		qint32 error;

		txt = NULL;
		pub = parent;
		group = NULL;
		browser = NULL;
		poll = avahi_qt_poll_get();
		if (!poll) {
			return;
		}
		client = avahi_client_new(poll, AVAHI_CLIENT_IGNORE_USER_CONFIG, NULL, this, &error);
		if (!client) {
			return;
		}
	}

	static void groupCallback(AvahiEntryGroup *g, AvahiEntryGroupState state, AVAHI_GCC_UNUSED void *userdata)
	{
		QZeroConfPrivate *ref = static_cast<QZeroConfPrivate *>(userdata);
		switch (state) {
			case AVAHI_ENTRY_GROUP_ESTABLISHED:
				emit ref->pub->servicePublished();
				break;
			case AVAHI_ENTRY_GROUP_COLLISION:
				avahi_entry_group_free(g);
				ref->group = NULL;
				emit ref->pub->error(QZeroConf::serviceNameCollision);
				break;
			case AVAHI_ENTRY_GROUP_FAILURE:
				avahi_entry_group_free(g);
				ref->group = NULL;
				emit ref->pub->error(QZeroConf::serviceRegistrationFailed);
				break;
			case AVAHI_ENTRY_GROUP_UNCOMMITED: break;
			case AVAHI_ENTRY_GROUP_REGISTERING: break;
		}
	}

	static void browseCallback(AvahiServiceBrowser *,
			AvahiIfIndex interface,
			AvahiProtocol protocol,
			AvahiBrowserEvent event,
			const char *name,
			const char *type,
			const char *domain,
			AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
			void* userdata)
	{
		QString key = name + QString::number(interface);
		QZeroConfPrivate *ref = static_cast<QZeroConfPrivate *>(userdata);

		QZeroConfService zcs;

		switch (event) {
		case AVAHI_BROWSER_FAILURE:
			ref->browserCleanUp();
			emit ref->pub->error(QZeroConf::browserFailed);
			break;
		case AVAHI_BROWSER_NEW:
			if (!ref->resolvers.contains(key))
				ref->resolvers.insert(key, avahi_service_resolver_new(ref->client, interface, protocol, name, type, domain, AVAHI_PROTO_UNSPEC, AVAHI_LOOKUP_USE_MULTICAST, resolveCallback, ref));
			break;
		case AVAHI_BROWSER_REMOVE:
			if (!ref->resolvers.contains(key))
				return;
			avahi_service_resolver_free(ref->resolvers[key]);
			ref->resolvers.remove(key);

			if (!ref->pub->services.contains(key))
				return;

			zcs = ref->pub->services[key];
			ref->pub->services.remove(key);
			emit ref->pub->serviceRemoved(zcs);
			break;
		case AVAHI_BROWSER_ALL_FOR_NOW:
		case AVAHI_BROWSER_CACHE_EXHAUSTED:
			break;
		}
	}

	static void resolveCallback(
	    AVAHI_GCC_UNUSED AvahiServiceResolver *r,
	    AVAHI_GCC_UNUSED AvahiIfIndex interface,
	    AVAHI_GCC_UNUSED AvahiProtocol protocol,
	    AvahiResolverEvent event,
	    const char *name,
	    const char *type,
	    const char *domain,
	    const char *host_name,
	    const AvahiAddress *address,
	    uint16_t port,
	    AvahiStringList *txt,
	    AvahiLookupResultFlags,
	    AVAHI_GCC_UNUSED void* userdata)
	{
		bool newRecord = 0;
		QZeroConfPrivate *ref = static_cast<QZeroConfPrivate *>(userdata);

		QString key = name + QString::number(interface);
		if (event == AVAHI_RESOLVER_FOUND) {
			if (!ref->pub->services.contains(key)) {
				newRecord = 1;
			}

			QZeroConfService& zcs  = ref->pub->services[key];

			zcs.setName(name);
			zcs.setType(type);
			zcs.setDomain(domain);
			zcs.setHost(host_name);
			zcs.setInterfaceIndex(interface);
			zcs.setPort(port);

			QMap<QByteArray, QByteArray> txt_map;
			while (txt)	// get txt records
			{
				QByteArray avahiText((const char *)txt->text, txt->size);
				QList<QByteArray> pair = avahiText.split('=');
				if (pair.size() == 2)
					txt_map[pair.at(0)] = pair.at(1);
				else
					txt_map[pair.at(0)] = "";
				txt = txt->next;
			}
			zcs.setTxt(txt_map);

			char a[AVAHI_ADDRESS_STR_MAX];
			avahi_address_snprint(a, sizeof(a), address);
			QHostAddress addr(a);
			if (protocol == AVAHI_PROTO_INET6)
				zcs.setIpv6(addr);
			else if (protocol == AVAHI_PROTO_INET)
				zcs.setIp(addr);

			if (newRecord)
				emit ref->pub->serviceAdded(zcs);
			else
				emit ref->pub->serviceUpdated(zcs);
		}
		else if (ref->pub->services.contains(key)) {	// delete service if exists and unable to resolve
			QZeroConfService zcs = ref->pub->services[key];
			ref->pub->services.remove(key);
			emit ref->pub->serviceRemoved(zcs);
			// don't delete the resolver here...we need to keep it around so Avahi will keep updating....might be able to resolve the service in the future
		}
	}

	void browserCleanUp(void)
	{
		if (!browser)
			return;
		avahi_service_browser_free(browser);
		browser = NULL;

		QMap<QString, QZeroConfService>::iterator i;
		for (i = pub->services.begin(); i != pub->services.end(); i++) {
			emit pub->serviceRemoved(i.value());
		}
		pub->services.clear();

		QMap<QString, AvahiServiceResolver *>::iterator r;
		for (r = resolvers.begin(); r != resolvers.end(); r++)
		    avahi_service_resolver_free(*r);
		resolvers.clear();
	}

	QZeroConf *pub;
	const AvahiPoll *poll;
	AvahiClient *client;
	AvahiEntryGroup *group;
	AvahiServiceBrowser *browser;
	QMap <QString, AvahiServiceResolver *> resolvers;
	QString name, type, domain;
	uint16_t port;
	AvahiStringList *txt;
};


QZeroConf::QZeroConf(QObject *parent) : QObject (parent)
{
	pri = new QZeroConfPrivate(this);
	qRegisterMetaType<QZeroConfService>("QZeroConfService");
}

QZeroConf::~QZeroConf()
{
	avahi_string_list_free(pri->txt);
	pri->browserCleanUp();
	if (pri->client)
		avahi_client_free(pri->client);
	delete pri;
}

void QZeroConf::startServicePublish(const char *name, const char *type, const char *domain, quint16 port)
{
	if (pri->group) {
		emit error(QZeroConf::serviceRegistrationFailed);
		return;
	}

	pri->name = name;
	pri->type = type;
	pri->domain = domain;
	pri->port = port;

	pri->group = avahi_entry_group_new(pri->client, QZeroConfPrivate::groupCallback, pri);

	int ret = avahi_entry_group_add_service_strlst(pri->group,
		AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, AVAHI_PUBLISH_UPDATE,
		name, type, domain, NULL, port, pri->txt);

	if (ret < 0) {
		avahi_entry_group_free(pri->group);
		pri->group = NULL;
		emit error(QZeroConf::serviceRegistrationFailed);
		return;
	}

	ret = avahi_entry_group_commit(pri->group);
	if (ret < 0) {
		pri->group = NULL;
		avahi_entry_group_free(pri->group);
		emit error(QZeroConf::serviceRegistrationFailed);
	}

	if (!pri->group)
		emit error(QZeroConf::serviceRegistrationFailed);
}

void QZeroConf::stopServicePublish(void)
{
	if (pri->group) {
		avahi_entry_group_free(pri->group);
		pri->group = NULL;
	}
}

bool QZeroConf::publishExists(void)
{
	if (pri->group)
		return true;
	else
		return false;
}

// http://www.zeroconf.org/rendezvous/txtrecords.html

void QZeroConf::addServiceTxtRecord(QString nameOnly)
{
	pri->txt = avahi_string_list_add(pri->txt, nameOnly.toUtf8());
}

void QZeroConf::addServiceTxtRecord(QString name, QString value)
{
	name.append("=");
	name.append(value);
	addServiceTxtRecord(name);
}

void QZeroConf::clearServiceTxtRecords()
{
	avahi_string_list_free(pri->txt);
	pri->txt = NULL;
}

void QZeroConf::updateServiceTxtRecords()
{
	if (!publishExists()) return;
	int err = avahi_entry_group_update_service_txt_strlst(pri->group,
		AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, (AvahiPublishFlags) 0,
		pri->name.toUtf8(), pri->type.toUtf8(), pri->domain.toUtf8(),
		pri->txt);

	if (err) emit error(QZeroConf::serviceRegistrationFailed);
}

void QZeroConf::startBrowser(QString type, QAbstractSocket::NetworkLayerProtocol protocol)
{
 	AvahiProtocol	avahiProtocol;

	if (pri->browser)
		emit error(QZeroConf::browserFailed);

	switch (protocol) {
		case QAbstractSocket::IPv4Protocol: avahiProtocol = AVAHI_PROTO_INET; break;
		case QAbstractSocket::IPv6Protocol: avahiProtocol = AVAHI_PROTO_INET6; break;
		case QAbstractSocket::AnyIPProtocol: avahiProtocol = AVAHI_PROTO_UNSPEC; break;
		default: avahiProtocol = AVAHI_PROTO_INET; break;
	};

	pri->browser = avahi_service_browser_new(pri->client, AVAHI_IF_UNSPEC, avahiProtocol, type.toUtf8(), NULL, AVAHI_LOOKUP_USE_MULTICAST, QZeroConfPrivate::browseCallback, pri);
	if (!pri->browser)
		emit error(QZeroConf::browserFailed);
}

void QZeroConf::stopBrowser(void)
{
	pri->browserCleanUp();
}

bool QZeroConf::browserExists(void)
{
	if (pri->browser)
		return true;
	else
		return false;
}
