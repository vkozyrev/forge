#ifndef DMZ_FORGE_QT_PLUGIN_CLIENT_DOT_H
#define DMZ_FORGE_QT_PLUGIN_CLIENT_DOT_H

#include <dmzForgeObserver.h>
#include <dmzRuntimePlugin.h>
#include <QtCore/QObject>


namespace dmz {

   class ForgeQtPluginClient : public QObject, public Plugin, public ForgeObserver {

      Q_OBJECT
      
      public:
         ForgeQtPluginClient (const PluginInfo &Info, Config &local);
         ~ForgeQtPluginClient ();

         // Plugin Interface
         virtual void update_plugin_state (
            const PluginStateEnum State,
            const UInt32 Level);

         virtual void discover_plugin (
            const PluginDiscoverEnum Mode,
            const Plugin *PluginPtr);
         
         // ForgeObserver Interface
         virtual void handle_reply (
            const UInt64 RequestId,
            const ForgeRequestTypeEnum ReqeustType,
            const Boolean Error,
            const StringContainer &Results);
         
      protected:
         void _init (Config &local);

         struct State;
         State &_state;
         
      private:
         ForgeQtPluginClient ();
         ForgeQtPluginClient (const ForgeQtPluginClient &);
         ForgeQtPluginClient &operator= (const ForgeQtPluginClient &);
   };
};


#endif // DMZ_FORGE_QT_PLUGIN_CLIENT_DOT_H