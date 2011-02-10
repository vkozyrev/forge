#ifndef DMZ_WEB_SERVICES_MODULE_QT_DOT_H
#define DMZ_WEB_SERVICES_MODULE_QT_DOT_H

#include <dmzArchiveObserverUtil.h>
#include <dmzObjectObserverUtil.h>
#include <dmzRuntimePlugin.h>
#include <dmzRuntimeTimeSlice.h>
#include <dmzWebServicesModule.h>
#include <dmzWebServicesObserver.h>
#include <QtCore/QObject>
#include <QtNetwork/QNetworkReply>

class QByteArray;
class QNetworkRequest;
class QString;


namespace dmz {

   class Config;


   class WebServicesModuleQt :
         public QObject,
         public Plugin,
         public TimeSlice,
         public WebServicesModule,
         public WebServicesObserver {

      Q_OBJECT

      public:
         WebServicesModuleQt (const PluginInfo &Info, Config &local);
         ~WebServicesModuleQt ();

         // Plugin Interface
         virtual void update_plugin_state (
            const PluginStateEnum State,
            const UInt32 Level);

         virtual void discover_plugin (
            const PluginDiscoverEnum Mode,
            const Plugin *PluginPtr);

         // TimeSlice Interface
         virtual void update_time_slice (const Float64 TimeDelta);

         // WebServicesModule Interface
         virtual Boolean publish_config (
            const String &Id,
            const Config &Data,
            WebServicesObserver &obs);

         virtual Boolean fetch_config (const String &Id, WebServicesObserver &obs);

         // WebServicesObserver Interface
         virtual void config_published (
            const String &Id,
            const Boolean Error,
            const Config &Data);

         virtual void config_fetched (
            const String &Id,
            const Boolean Error,
            const Config &Data);

      protected Q_SLOTS:
         void _authenticate (QNetworkReply *reply, QAuthenticator *authenticator);

         void _reply_aborted (const UInt64 RequestId);

         void _reply_download_progress (
            const UInt64 RequestId,
            QNetworkReply *reply,
            qint64 bytesReceived,
            qint64 bytesTotal);

         void _reply_finished (const UInt64 RequestId, QNetworkReply *reply);

      protected:
         struct DocStruct;
         struct RequestStruct;

         void _handle_reply (RequestStruct &request);

//          void _handle_error (
//             const UInt64 RequestId,
//             const Int32 StatusCode,
//             const QString &ErrorMessage);

         RequestStruct *_publish_session (const Handle SessionHandle);

         RequestStruct *_publish_document (
            const String &Id,
            const Config &Data,
            WebServicesObserver &obs);

         RequestStruct *_fetch_document (const String &Id, WebServicesObserver &obs);

         RequestStruct *_fetch_changes (
            const Int32 Since,
            const Boolean Continuous = False);

         void _document_published (RequestStruct &request);
         void _document_fetched (RequestStruct &request);

         void _changes_fetched (RequestStruct &request);

         Boolean _handle_continuous_feed (RequestStruct &request);

         QUrl _get_url (const String &EndPoint) const;
         QUrl _get_root_url (const String &EndPoint) const;

         void _init (Config &local);

      protected:
         struct State;
         State &_state;

      private:
         WebServicesModuleQt ();
         WebServicesModuleQt (const WebServicesModuleQt &);
         WebServicesModuleQt &operator= (const WebServicesModuleQt &);
   };
};

#endif // DMZ_WEB_SERVICES_MODULE_QT_DOT_H
