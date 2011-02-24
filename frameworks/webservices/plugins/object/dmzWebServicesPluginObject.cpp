#include <dmzObjectAttributeMasks.h>
#include <dmzObjectConsts.h>
#include <dmzObjectModule.h>
#include "dmzWebServicesPluginObject.h"
#include <dmzRuntimeConfigToTypesBase.h>
#include <dmzRuntimeConfigToMatrix.h>
#include <dmzRuntimeConfigToVector.h>
#include <dmzRuntimeConfigWrite.h>
#include <dmzRuntimeData.h>
#include <dmzRuntimeObjectType.h>
#include <dmzRuntimePluginFactoryLinkSymbol.h>
#include <dmzRuntimePluginInfo.h>
#include <dmzTypesMask.h>
#include <dmzTypesMatrix.h>
#include <dmzTypesVector.h>
#include <dmzTypesUUID.h>
#include <dmzWebServicesModule.h>

using namespace dmz;

namespace {

static Mask
local_config_to_mask (Config config, Log &log) {

   Mask result;

   ConfigIterator it;
   Config mask;
   Boolean error (False);

   while (config.get_next_config (it, mask)) {

      if (mask.get_name () == "mask") {

         result |=
            string_to_object_attribute_mask (config_to_string ("name", mask), &log);
      }
   }

   if (!result && !error) {

      log.info << "Filtering all attribute types." << endl;
      result = ObjectAllMask;
   }

   return result;
}

};


dmz::WebServicesPluginObject::WebServicesPluginObject (const PluginInfo &Info, Config &local) :
      Plugin (Info),
      TimeSlice (Info),
      WebServicesObserver (Info),
      MessageObserver (Info),
      ObjectObserverUtil (Info, local),
      _log (Info),
      _defs (Info.get_context ()),
      _webservices (0),
      _filterList (0),
      _defaultHandle (0),
      _lastSeq (0),
      _inDump (False),
      _inUpdate (False),
      _upToDate (False),
      _authenticated (False),
      _publishRate (2.0),
      _publishDelta (0.0) {

   _init (local);
}


dmz::WebServicesPluginObject::~WebServicesPluginObject () {

   delete_list (_filterList);
   _filterList = 0;

   _objectLinkTable.empty ();
   _configTable.empty ();
}


// Plugin Interface
void
dmz::WebServicesPluginObject::update_plugin_state (
      const PluginStateEnum State,
      const UInt32 Level) {

   if (State == PluginStateInit) {

   }
   else if (State == PluginStateStart) {

   }
   else if (State == PluginStateStop) {

   }
   else if (State == PluginStateShutdown) {

   }
}


void
dmz::WebServicesPluginObject::discover_plugin (
      const PluginDiscoverEnum Mode,
      const Plugin *PluginPtr) {

   if (Mode == PluginDiscoverAdd) {

      if (!_webservices) {

         _webservices = WebServicesModule::cast (PluginPtr, _webservicesName);
      }
   }
   else if (Mode == PluginDiscoverRemove) {

      if (_webservices && (_webservices == WebServicesModule::cast (PluginPtr))) {

         _webservices = 0;
      }
   }
}


// TimeSlice Interface
void
dmz::WebServicesPluginObject::update_time_slice (const Float64 TimeDelta) {

   ObjectModule *objMod (get_object_module ());

   if (_webservices && objMod) {

      _publishDelta += TimeDelta;

      if (_publishDelta > _publishRate) {

         _publishDelta = 0;

         if (_deleteTable.get_count ()) {

            _webservices->delete_configs (_deleteTable, *this);
            _deleteTable.clear ();
         }

         if (_publishTable.get_count ()) {

            HandleContainerIterator it;
            Handle objHandle (_publishTable.get_first (it));

            while (objHandle) {

               if (_publish (objHandle)) {

                  _publishTable.remove (objHandle);
               }

               objHandle = _publishTable.get_next (it);
            }
         }
      }

      if (_objectLinkTable.get_count ()) {

         HashTableStringIterator objIt;
         ObjectLinkStruct *objLink = _objectLinkTable.get_first (objIt);
         while (objLink) {

            _link_to_sub (*objLink);

            if (objLink->inLinks.get_count () == 0) {

               if (_objectLinkTable.remove (objLink->SubName)) {

                  delete objLink; objLink = 0;
               }
            }

            objLink = _objectLinkTable.get_next (objIt);
         }
      }

      if (_fetchTable.get_count ()) {

         _webservices->fetch_configs (_fetchTable, *this);
         _pendingFetchTable += _fetchTable;
         _fetchTable.clear ();
      }
   }
}


// WebServicesObserver Interface
void
dmz::WebServicesPluginObject::config_published (
      const String &Id,
      const Boolean Error,
      const Config &Data) {

   _pendingPublishTable.remove (Id);

   if (Error) {

      _log.warn << "Error: " << config_to_string ("reason", Data) << endl;
   }
   else {

   }
}


void
dmz::WebServicesPluginObject::config_fetched (
   const String &Id,
   const Boolean Error,
   const Config &Data) {

   _pendingFetchTable.remove (Id);

   if (Error) {

   }
   else {

_log.warn << "config_fetched: " << Id << endl;
      _inUpdate = True;
      _config_to_object (Data);
      _inUpdate = False;
   }
}


void
dmz::WebServicesPluginObject::config_deleted (
      const String &Id,
      const Boolean Error,
      const Config &Data) {

   if (Error) {

   }
   else {

      StringContainer container;
      container.add (Id);

      _configs_deleted (container);
   }
}


void
dmz::WebServicesPluginObject::config_updated (
      const String &Id,
      const Boolean Deleted,
      const Int32 Sequence,
      const Config &Data) {

   if (Deleted) {

      StringContainer container;
      container.add (Id);

      _configs_deleted (container);
   }
   else {

      if (Data) { config_fetched (Id, False, Data); }
      else { _fetch (Id); }
   }

   _lastSeq = Sequence;
_log.warn << "config_updated[" << _lastSeq << "]: " << Id << endl;
   _upToDate = True;
}


void
dmz::WebServicesPluginObject::config_updated (
      const StringContainer &UpdatedIdList,
      const StringContainer &DeletedIdList,
      const Int32 LastSequence) {

   _configs_deleted (DeletedIdList);

   if (_webservices && UpdatedIdList.get_count ()) {

      _webservices->fetch_configs (UpdatedIdList, *this);
      _pendingFetchTable += UpdatedIdList;
   }

   _lastSeq = LastSequence;
}


// Message Observer Interface
void
dmz::WebServicesPluginObject::receive_message (
      const Message &Type,
      const UInt32 MessageSendHandle,
      const Handle TargetObserverHandle,
      const Data *InData,
      Data *outData) {

   if (Type == _loginSuccessMsg) {

      if (InData) {

//         String username;
//         InData->lookup_string (_usernameHandle, 0, username);
      }

      _authenticated = True;

      if (_webservices) {

         if (_upToDate) {

            _webservices->start_realtime_updates (*this);
         }
         else {

            _webservices->get_config_updates (*this, _lastSeq);
         }
      }
   }
   else if (Type == _loginFailedMsg) {

      _authenticated = False;

      if (_webservices) {

//         _webservices->stop_realtime_updates (*this);
      }
   }
   else if (Type == _logoutMsg) {

      _authenticated = False;

      if (_webservices) {

//         _webservices->stop_realtime_updates (*this);
      }
   }
}


// Object Observer Interface
void
dmz::WebServicesPluginObject::create_object (
      const UUID &Identity,
      const Handle ObjectHandle,
      const ObjectType &Type,
      const ObjectLocalityEnum Locality) {

   if (_handle_type (Type)) {

      if (_inDump) { /* do nothing */ }
      else {

         _activeTable.add (ObjectHandle);
         _update (ObjectHandle);
      }
   }
}


void
dmz::WebServicesPluginObject::destroy_object (
      const UUID &Identity,
      const Handle ObjectHandle) {

   if (_inDump) { /* do nothing */ }
   else if (_activeTable.contains (ObjectHandle)) {

      _publishTable.remove (ObjectHandle);

      const String Id (Identity.to_string ());

      if (_inUpdate) { _deleteTable.remove (Id); }
      else { _deleteTable.add (Id); }

      _activeTable.remove (ObjectHandle);
   }
}


void
dmz::WebServicesPluginObject::update_object_uuid (
      const Handle ObjectHandle,
      const UUID &Identity,
      const UUID &PrevIdentity) {

   if (_inDump) { /* do nothing */ }
   else {

//      if (_activeTable.contains (ObjectHandle)) {

//         _deleteTable.add (PrevIdentity.to_string ());
//         _updateTable.add (Identity.to_string ());
//      }
   }
}


void
dmz::WebServicesPluginObject::remove_object_attribute (
      const UUID &Identity,
      const Handle ObjectHandle,
      const Handle AttributeHandle,
      const Mask &AttrMask) {

   if (_active (ObjectHandle) && _handle_attribute (AttributeHandle, AttrMask)) {

      _update (ObjectHandle);
   }
}


void
dmz::WebServicesPluginObject::update_object_locality (
      const UUID &Identity,
      const Handle ObjectHandle,
      const ObjectLocalityEnum Locality,
      const ObjectLocalityEnum PrevLocality) {

   if (_active (ObjectHandle)) {

   }
}


void
dmz::WebServicesPluginObject::link_objects (
      const Handle LinkHandle,
      const Handle AttributeHandle,
      const UUID &SuperIdentity,
      const Handle SuperHandle,
      const UUID &SubIdentity,
      const Handle SubHandle) {

   if (_active (SuperHandle) && _handle_attribute (AttributeHandle, ObjectLinkMask)) {

      _update (SuperHandle);

      if (_inDump) {

         Config config;
         if (_get_attr_config (AttributeHandle, config)) {

            Config links;
            if (!config.lookup_config ("links", links)) {

               Config tmp ("links");
               links = tmp;
               config.add_config (links);
            }

            Config obj ("object");
            obj.store_attribute ("name", SubIdentity.to_string ());

            ObjectModule *objMod (get_object_module ());
            if (objMod) {

               UUID uuid;

               const Handle LinkAttrHandle (
                  objMod->lookup_link_attribute_object (LinkHandle));

               if (LinkAttrHandle && objMod->lookup_uuid (LinkAttrHandle, uuid)) {

                  obj.store_attribute ("attribute", uuid.to_string ());
               }
            }

            links.add_config (obj);
         }
      }
   }
}


void
dmz::WebServicesPluginObject::unlink_objects (
      const Handle LinkHandle,
      const Handle AttributeHandle,
      const UUID &SuperIdentity,
      const Handle SuperHandle,
      const UUID &SubIdentity,
      const Handle SubHandle) {

   if (_active (SuperHandle) &&
         _handle_attribute (AttributeHandle, ObjectUnlinkMask)) {

      _update (SuperHandle);
   }
}


void
dmz::WebServicesPluginObject::update_link_attribute_object (
      const Handle LinkHandle,
      const Handle AttributeHandle,
      const UUID &SuperIdentity,
      const Handle SuperHandle,
      const UUID &SubIdentity,
      const Handle SubHandle,
      const UUID &AttributeIdentity,
      const Handle AttributeObjectHandle,
      const UUID &PrevAttributeIdentity,
      const Handle PrevAttributeObjectHandle) {

   if (_active (SuperHandle) &&
         _handle_attribute (AttributeHandle, ObjectLinkAttributeMask)) {

      _update (SuperHandle);
   }
}


void
dmz::WebServicesPluginObject::update_object_counter (
      const UUID &Identity,
      const Handle ObjectHandle,
      const Handle AttributeHandle,
      const Int64 Value,
      const Int64 *PreviousValue) {

   if (_active (ObjectHandle) &&
         _handle_attribute (AttributeHandle, ObjectCounterMask)) {

      _update (ObjectHandle);

      if (_inDump) {

         Config config;
         if (_get_attr_config (AttributeHandle, config)) {

            Config counter;
            if (!config.lookup_config ("counter", counter)) {

               counter = Config ("counter");
               config.add_config (counter);
            }

            String valueStr; valueStr << Value;
            config.store_attribute ("value", valueStr);

            ObjectModule *objMod (get_object_module ());
            if (objMod) {

               Boolean rollover (False);
               objMod->lookup_counter_rollover (ObjectHandle, AttributeHandle, rollover);
               config.store_attribute ("rollover", (rollover ? "true" : "false"));
            }
         }
      }
   }
}


void
dmz::WebServicesPluginObject::update_object_counter_minimum (
      const UUID &Identity,
      const Handle ObjectHandle,
      const Handle AttributeHandle,
      const Int64 Value,
      const Int64 *PreviousValue) {

   if (_active (ObjectHandle) &&
         _handle_attribute (AttributeHandle, ObjectMinCounterMask)) {

      _update (ObjectHandle);

      if (_inDump) {

         Config config;
         if (_get_attr_config (AttributeHandle, config)) {

            Config counter;
            if (!config.lookup_config ("counter", counter)) {

               counter = Config ("counter");
               config.add_config (counter);
            }

            String valueStr; valueStr << Value;
            config.store_attribute ("minimum", valueStr);
         }
      }
   }
}


void
dmz::WebServicesPluginObject::update_object_counter_maximum (
      const UUID &Identity,
      const Handle ObjectHandle,
      const Handle AttributeHandle,
      const Int64 Value,
      const Int64 *PreviousValue) {

   if (_active (ObjectHandle) &&
         _handle_attribute (AttributeHandle, ObjectMaxCounterMask)) {

      _update (ObjectHandle);

      if (_inDump) {

         Config config;
         if (_get_attr_config (AttributeHandle, config)) {

            Config counter;
            if (!config.lookup_config ("counter", counter)) {

               counter = Config ("counter");
               config.add_config (counter);
            }

            String valueStr; valueStr << Value;
            config.store_attribute ("maximum", valueStr);
         }
      }
   }
}


void
dmz::WebServicesPluginObject::update_object_alternate_type (
      const UUID &Identity,
      const Handle ObjectHandle,
      const Handle AttributeHandle,
      const ObjectType &Value,
      const ObjectType *PreviousValue) {

   if (_active (ObjectHandle) &&
         _handle_attribute (AttributeHandle, ObjectAltTypeMask)) {

      _update (ObjectHandle);

      if (_inDump) {

         Config config;
         if (_get_attr_config (AttributeHandle, config)) {

            Config type ("alttype");
            type.store_attribute ("value", Value.get_name ());
            config.add_config (type);
         }
      }
   }
}


void
dmz::WebServicesPluginObject::update_object_state (
      const UUID &Identity,
      const Handle ObjectHandle,
      const Handle AttributeHandle,
      const Mask &Value,
      const Mask *PreviousValue) {

   if (_active (ObjectHandle) &&
         _handle_attribute (AttributeHandle, ObjectStateMask)) {

      _update (ObjectHandle);

      if (_inDump) {

         Config config;
         if (Value && _get_attr_config (AttributeHandle, config)) {

            const Mask FilteredValue (_filter_state (AttributeHandle, Value));
            if (FilteredValue) {

               Config state ("state");
               String name;
               _defs.lookup_state_name (FilteredValue, name);

               if (name) {

                  state.store_attribute ("value", name);
                  config.add_config (state);
               }
            }
         }
      }
   }
}


void
dmz::WebServicesPluginObject::update_object_flag (
      const UUID &Identity,
      const Handle ObjectHandle,
      const Handle AttributeHandle,
      const Boolean Value,
      const Boolean *PreviousValue) {

   if (_active (ObjectHandle) &&
         _handle_attribute (AttributeHandle, ObjectFlagMask)) {

      _update (ObjectHandle);

      if (_inDump) {

         Config config;
         if (_get_attr_config (AttributeHandle, config)) {

            config.add_config (boolean_to_config ("flag", "value", Value));
         }
      }
   }
}


void
dmz::WebServicesPluginObject::update_object_time_stamp (
      const UUID &Identity,
      const Handle ObjectHandle,
      const Handle AttributeHandle,
      const Float64 Value,
      const Float64 *PreviousValue) {

   if (_active (ObjectHandle) &&
         _handle_attribute (AttributeHandle, ObjectTimeStampMask)) {

      _update (ObjectHandle);

      if (_inDump) {

         Config config;
         if (_get_attr_config (AttributeHandle, config)) {

            config.add_config (float64_to_config ("timestamp", "value", Value));
         }
      }
   }
}


void
dmz::WebServicesPluginObject::update_object_position (
      const UUID &Identity,
      const Handle ObjectHandle,
      const Handle AttributeHandle,
      const Vector &Value,
      const Vector *PreviousValue) {

   if (_active (ObjectHandle) &&
         _handle_attribute (AttributeHandle, ObjectPositionMask)) {

      _update (ObjectHandle);

      if (_inDump) {

         Config config;
         if (_get_attr_config (AttributeHandle, config)) {

            config.add_config (vector_to_config ("position", Value));
         }
      }
   }
}


void
dmz::WebServicesPluginObject::update_object_orientation (
      const UUID &Identity,
      const Handle ObjectHandle,
      const Handle AttributeHandle,
      const Matrix &Value,
      const Matrix *PreviousValue) {

   if (_active (ObjectHandle) &&
         _handle_attribute (AttributeHandle, ObjectOrientationMask)) {

      _update (ObjectHandle);

      if (_inDump) {

         Config config;
         if (_get_attr_config (AttributeHandle, config)) {

            config.add_config (matrix_to_config ("orientation", Value));
         }
      }
   }
}


void
dmz::WebServicesPluginObject::update_object_velocity (
      const UUID &Identity,
      const Handle ObjectHandle,
      const Handle AttributeHandle,
      const Vector &Value,
      const Vector *PreviousValue) {

   if (_active (ObjectHandle) &&
         _handle_attribute (AttributeHandle, ObjectVelocityMask)) {

      _update (ObjectHandle);

      if (_inDump) {

         Config config;
         if (_get_attr_config (AttributeHandle, config)) {

            config.add_config (vector_to_config ("velocity", Value));
         }
      }
   }
}


void
dmz::WebServicesPluginObject::update_object_acceleration (
      const UUID &Identity,
      const Handle ObjectHandle,
      const Handle AttributeHandle,
      const Vector &Value,
      const Vector *PreviousValue) {

   if (_active (ObjectHandle) &&
         _handle_attribute (AttributeHandle, ObjectAccelerationMask)) {

      _update (ObjectHandle);

      if (_inDump) {

         Config config;
         if (_get_attr_config (AttributeHandle, config)) {

            config.add_config (vector_to_config ("acceleration", Value));
         }
      }
   }
}


void
dmz::WebServicesPluginObject::update_object_scale (
      const UUID &Identity,
      const Handle ObjectHandle,
      const Handle AttributeHandle,
      const Vector &Value,
      const Vector *PreviousValue) {

   if (_active (ObjectHandle) &&
         _handle_attribute (AttributeHandle, ObjectScaleMask)) {

      _update (ObjectHandle);

      if (_inDump) {

         Config config;
         if (_get_attr_config (AttributeHandle, config)) {

            config.add_config (vector_to_config ("scale", Value));
         }
      }
   }
}


void
dmz::WebServicesPluginObject::update_object_vector (
      const UUID &Identity,
      const Handle ObjectHandle,
      const Handle AttributeHandle,
      const Vector &Value,
      const Vector *PreviousValue) {

   if (_active (ObjectHandle) &&
         _handle_attribute (AttributeHandle, ObjectVectorMask)) {

      _update (ObjectHandle);

      if (_inDump) {

         Config config;
         if (_get_attr_config (AttributeHandle, config)) {

            config.add_config (vector_to_config ("vector", Value));
         }
      }
   }
}


void
dmz::WebServicesPluginObject::update_object_scalar (
      const UUID &Identity,
      const Handle ObjectHandle,
      const Handle AttributeHandle,
      const Float64 Value,
      const Float64 *PreviousValue) {

   if (_active (ObjectHandle) &&
         _handle_attribute (AttributeHandle, ObjectScalarMask)) {

      _update (ObjectHandle);

      if (_inDump) {

         Config config;
         if (_get_attr_config (AttributeHandle, config)) {

            config.add_config (float64_to_config ("scalar", Value));
         }
      }
   }
}


void
dmz::WebServicesPluginObject::update_object_text (
      const UUID &Identity,
      const Handle ObjectHandle,
      const Handle AttributeHandle,
      const String &Value,
      const String *PreviousValue) {

   if (_active (ObjectHandle) &&
         _handle_attribute (AttributeHandle, ObjectTextMask)) {

      _update (ObjectHandle);

      if (_inDump) {

         Config config;
         if (_get_attr_config (AttributeHandle, config)) {

            config.add_config (string_to_config ("text", "value", Value));
         }
      }
   }
}


void
dmz::WebServicesPluginObject::update_object_data (
      const UUID &Identity,
      const Handle ObjectHandle,
      const Handle AttributeHandle,
      const Data &Value,
      const Data *PreviousValue) {

   if (_active (ObjectHandle) &&
         _handle_attribute (AttributeHandle, ObjectDataMask)) {

      _update (ObjectHandle);

      if (_inDump) {

         Config config;

         if (_get_attr_config (AttributeHandle, config)) {

            config.add_config (data_to_config (Value, get_plugin_runtime_context (), &_log));
         }
      }
   }
}


// WebServicesPluginObject Interface
dmz::Boolean
dmz::WebServicesPluginObject::_publish (const Handle ObjectHandle) {

   Boolean result (False);

   ObjectModule *objMod (get_object_module ());
   if (ObjectHandle && objMod && _webservices) {

      UUID uuid;
      if (objMod->lookup_uuid (ObjectHandle, uuid)) {

         const String DocId (uuid.to_string ());

         if (!_pendingPublishTable.contains (DocId)) {

            Config doc = _archive_object (ObjectHandle);
            if (doc) {

               if (_webservices->publish_config (DocId, doc, *this)) {

                  _pendingPublishTable.add (DocId);
                  result = True;
               }
               else {

                  _log.error << "publish failed" << endl;
               }
            }
         }
      }
   }

   return result;
}


dmz::Boolean
dmz::WebServicesPluginObject::_fetch (const String &Id) {

   Boolean result (False);

   if (Id) {

      if (!_pendingFetchTable.contains (Id)) {

         _fetchTable.add (Id);

//         if (_webservices->fetch_config (Id, *this)) {

//            _pendingFetchTable.add (Id);
//            result = True;
//         }
      }
   }

   return result;
}


void
dmz::WebServicesPluginObject::_link_to_sub (ObjectLinkStruct &objLink) {

   ObjectModule *objMod (get_object_module ());
   if (objMod) {

      Handle subHandle = objMod->lookup_handle_from_uuid (objLink.SubName);
      if (!subHandle) {

//         _fetchTable.add (objLink.SubName);
         _fetch (objLink.SubName);
      }
      else {

         HashTableHandleIterator linkIt;
         LinkStruct *ls = objLink.inLinks.get_first (linkIt);
         while (ls) {

            Boolean removeLink (False);

            if (!ls->linkHandle) {

               _inUpdate = True;

               ls->linkHandle = objMod->link_objects (
                  ls->AttrHandle,
                  ls->SuperHandle,
                  subHandle);

               _inUpdate = False;
            }

            if (ls->linkHandle) {

               if (!(ls->attrObjectName)) {

                  _log.info << "link done, no attr obj" << endl;
                  removeLink = True;
               }
               else {

                  Handle attrObjectHandle =
                        objMod->lookup_handle_from_uuid (ls->attrObjectName);

                  if (!attrObjectHandle) {

//                     _fetchTable.add (ls->attrObjectName);
                     _fetch (ls->attrObjectName);
                  }
                  else {

                     _inUpdate = True;

                     objMod->store_link_attribute_object (
                        ls->linkHandle,
                        attrObjectHandle);

                     _inUpdate = False;

                     _log.info << "link done, link attr stored" << endl;
                     removeLink = True;
                  }
               }
            }

            if (removeLink) {

               if (objLink.inLinks.remove (ls->SuperHandle)) {

_log.debug << "remove superHandle: " << ls->SuperHandle << endl;

                  delete ls; ls = 0;
               }

_log.debug << "in links: " << objLink.inLinks.get_count () << endl;
            }

            ls = objLink.inLinks.get_next (linkIt);
         }
      }
   }
}


void
dmz::WebServicesPluginObject::_configs_deleted (const StringContainer &DeleteIdList) {

   ObjectModule *objMod (get_object_module ());
   if(objMod) {

      String id;
      StringContainerIterator it;

      while (DeleteIdList.get_next (it, id)) {

         Handle objectHandle = objMod->lookup_handle_from_uuid (UUID (id));
         if (objectHandle) {

            _inUpdate = True;
            objMod->destroy_object (objectHandle);
            _inUpdate = False;

            _activeTable.remove (objectHandle);
//            _pendingTable.remove (id);
            _deleteTable.remove (id);
         }
      }
   }
}


dmz::Config
dmz::WebServicesPluginObject::_archive_object (const Handle ObjectHandle) {

   Config result;

   ObjectModule *objMod (get_object_module ());
   if (objMod) {

      const ObjectType Type (objMod->lookup_object_type (ObjectHandle));
      if (Type) {

         if (_handle_type (Type)) {

            UUID uuid;
            if (objMod->lookup_uuid (ObjectHandle, uuid)) {

               Config tmp ("object");

               tmp.store_attribute ("type", Type.get_name ());
               tmp.store_attribute ("uuid", uuid.to_string ());

               _currentConfig = result = tmp;

               _inDump = True;
               objMod->dump_all_object_attributes (ObjectHandle, *this);
               _inDump = False;
            }
         }
      }
   }

   _configTable.empty ();
   _currentConfig.set_config_context (0);

   return result;
}


dmz::Boolean
dmz::WebServicesPluginObject::_get_attr_config (
      const Handle AttrHandle,
      Config &config) {

   Boolean result (False);

   Config *ptr = _configTable.lookup (AttrHandle);
   if (!ptr) {

      ptr = new Config ("attributes");
      if (ptr) {

         if (AttrHandle != _defaultHandle) {

            const String Name (_defs.lookup_named_handle_name (AttrHandle));

            ptr->store_attribute ("name", Name);
         }

         if (!_configTable.store (AttrHandle, ptr)) { delete ptr; ptr = 0; }

         if (ptr) { _currentConfig.add_config (*ptr); }
      }
   }

   if (ptr) { config = *ptr; result = True; }

   return result;
}


void
dmz::WebServicesPluginObject::_config_to_object (const Config &Data) {

   ObjectModule *objMod (get_object_module ());
   if (objMod) {

      const UUID ObjUUID (config_to_string ("uuid", Data));
      String objectName (config_to_string ("name", Data));
      if (!objectName) { objectName = ObjUUID.to_string (); }

      const String TypeName (config_to_string ("type", Data));
      const ObjectType Type (TypeName, get_plugin_runtime_context ());

      if (Type) {

         Handle objectHandle (0);

         if (ObjUUID) { objectHandle = objMod->lookup_handle_from_uuid (ObjUUID); }

         if (!objectHandle) { objectHandle = objMod->create_object (Type, ObjectLocal); }

         if (objectHandle) {

            if (ObjUUID) { objMod->store_uuid (objectHandle, ObjUUID); }

            Config attrList;

            if (Data.lookup_all_config ("attributes", attrList)) {

               ConfigIterator it;
               Config attrData;

               Boolean found (attrList.get_first_config (it, attrData));
               while (found) {

                  _config_to_object_attributes (objectHandle, attrData);
                  found = attrList.get_next_config (it, attrData);
               }
            }

            objMod->activate_object (objectHandle);
         }
         else {

            _log.error << "Unable to create object of type: " << TypeName << endl;
         }
      }
      else if (!Type) {

         _log.error << "Unable to create object of unknown type: " << TypeName << endl;
      }
      else {

         _log.info << "Filtering object with type: " << TypeName << endl;
      }
   }
}


void
dmz::WebServicesPluginObject::_config_to_object_attributes (
      const Handle ObjectHandle,
      const Config &AttrData) {

   ObjectModule *objMod (get_object_module ());
   if (objMod) {

      const String AttributeName (
         config_to_string ("name", AttrData, ObjectAttributeDefaultName));

      const Handle AttrHandle (_defs.create_named_handle (AttributeName));

      Config data;
      ConfigIterator it;

      Boolean found (AttrData.get_first_config (it, data));

      while (found) {

         const String DataName (data.get_name ().to_lower ());

         if (DataName == "links") {

            Config linkList;

            data.lookup_all_config ("object", linkList);

            Config obj;
            ConfigIterator it;

            Boolean found (linkList.get_first_config (it, obj));

            while (found) {

               const String Name (config_to_string ("name", obj));
               if (Name) {

                  const String AttrObjName (config_to_string ("attribute", obj));
                  _add_sub_link (AttrHandle, ObjectHandle, Name, AttrObjName);
               }

               found = linkList.get_next_config (it, obj);
            }
         }
         else if (DataName == "counter") {

            String valueStr;
            if (data.lookup_attribute ("minimum", valueStr)) {

               objMod->store_counter_minimum (
                  ObjectHandle,
                  AttrHandle,
                  string_to_int64 (valueStr));
            }

            if (data.lookup_attribute ("maximum", valueStr)) {

               objMod->store_counter_maximum (
                  ObjectHandle,
                  AttrHandle,
                  string_to_int64 (valueStr));
            }

            if (data.lookup_attribute ("value", valueStr)) {

                objMod->store_counter (
                   ObjectHandle,
                   AttrHandle,
                   string_to_int64 (valueStr));

                objMod->store_counter_rollover (
                   ObjectHandle,
                   AttrHandle,
                   config_to_boolean ("rollover", data, False));
             }
         }
         else if (DataName == "alttype") {

            const ObjectType Type (
               config_to_string ("value", data),
               get_plugin_runtime_context ());

            objMod->store_alternate_object_type (ObjectHandle, AttrHandle, Type);
         }
         else if (DataName == "state") {

            Mask value;
            _defs.lookup_state (config_to_string ("value", data), value);

            objMod->store_state (ObjectHandle, AttrHandle, value);
         }
         else if (DataName == "flag") {

            objMod->store_flag (
               ObjectHandle,
               AttrHandle,
               config_to_boolean ("value", data));
         }
         else if (DataName == "timestamp") {

            objMod->store_time_stamp (
               ObjectHandle,
               AttrHandle,
               config_to_float64 (data));
         }
         else if (DataName == "position") {

            objMod->store_position (ObjectHandle, AttrHandle, config_to_vector (data));
         }
         else if (DataName == "orientation") {

            objMod->store_orientation (
               ObjectHandle,
               AttrHandle,
               config_to_matrix (data));
         }
         else if (DataName == "euler") {

            const Vector Euler (config_to_vector (data));
            const Matrix Value (Euler.get_x (), Euler.get_y (), Euler.get_z ());

            objMod->store_orientation (ObjectHandle, AttrHandle, Value);
         }
         else if (DataName == "velocity") {

            objMod->store_velocity (ObjectHandle, AttrHandle, config_to_vector (data));
         }
         else if (DataName == "acceleration") {

            objMod->store_acceleration (
               ObjectHandle,
               AttrHandle,
               config_to_vector (data));
         }
         else if (DataName == "scale") {

            objMod->store_scale (ObjectHandle, AttrHandle, config_to_vector (data));
         }
         else if (DataName == "vector") {

            objMod->store_vector (ObjectHandle, AttrHandle, config_to_vector (data));
         }
         else if (DataName == "scalar") {

            Float64 value (config_to_float64 (data));
            objMod->store_scalar (ObjectHandle, AttrHandle, value);
         }
         else if (DataName == "text") {

            objMod->store_text (ObjectHandle, AttrHandle, config_to_string (data));
         }
         else if (DataName == "data") {

            Data value;
            if (config_to_data (data, get_plugin_runtime_context (), value, &_log)) {

               objMod->store_data (ObjectHandle, AttrHandle, value);
            }
         }
         else {

            _log.error << "Unsupported attribute type: " << data.get_name () << endl;
         }

         found = AttrData.get_next_config (it, data);
      }
   }
}


dmz::Boolean
dmz::WebServicesPluginObject::_add_sub_link (
      const Handle AttrHandle,
      const Handle SuperHandle,
      const String &SubName,
      const String &AttrObjName) {

   Boolean result (False);

   ObjectLinkStruct *objLink = _objectLinkTable.lookup (SubName);
   if (!objLink) {

      objLink = new ObjectLinkStruct (SubName);
      if (!_objectLinkTable.store (SubName, objLink)) {

         delete objLink; objLink = 0;
      }
   }

   if (objLink) {

      LinkStruct *ls = new LinkStruct (AttrHandle, SuperHandle);
      ls->subName = SubName;
      ls->attrObjectName = AttrObjName;

      if (objLink->inLinks.store (SuperHandle, ls)) { result = True; }
      else { delete ls; ls = 0; }
   }

   return result;
}

dmz::Boolean
dmz::WebServicesPluginObject::_handle_type (const ObjectType &Type) {

   Boolean result (True);

   FilterStruct *filter (_filterList);
   while (filter) {

      if (filter->exTypes.get_count () && filter->exTypes.contains_type (Type)) {

         result = False;
         filter = 0;
      }

      if (filter && filter->inTypes.get_count () &&
            !filter->inTypes.contains_type (Type)) {

         result = False;
         filter = 0;
      }

      if (filter) { filter = filter->next; }
   }

   return result;
}


dmz::Boolean
dmz::WebServicesPluginObject::_handle_attribute (
      const Handle AttrHandle,
      const Mask &AttrMask) {

   Boolean result (True);

   if (_filterList) {

      Mask filterMask;

      FilterStruct *filter (_filterList);
      while (filter) {

         Mask *maskPtr = filter->attrTable.lookup (AttrHandle);

         if (!maskPtr) {

            FilterAttrStruct *current (filter->list);

            if (current) {

               const String AttrName (_defs.lookup_named_handle_name (AttrHandle));

               while (current && !maskPtr) {

                  Int32 place (-1);

                  if (AttrName.find_sub (current->Name, place)) {

                     maskPtr = new Mask (current->Attr);

                     if (maskPtr && !filter->attrTable.store (AttrHandle, maskPtr)) {

                        delete maskPtr; maskPtr = 0;
                     }
                  }

                  current = current->next;
               }
            }
         }

         if (maskPtr) { filterMask |= *maskPtr; }

         filter = filter->next;
      }

      if (filterMask.contains (AttrMask)) { result = False; }
   }

   return result;
}


dmz::Mask
dmz::WebServicesPluginObject::_filter_state (
      const Handle AttrHandle,
      const Mask &Value) {

   Mask result (Value);

   FilterStruct *filter (_filterList);
   while (filter) {

      Mask *filterMask (filter->stateTable.lookup (AttrHandle));
      if (filterMask) { result.unset (*filterMask); }

      filter = filter->next;
   }

   return result;
}


dmz::Boolean
dmz::WebServicesPluginObject::_active (const Handle ObjectHandle) {

   return _activeTable.contains (ObjectHandle);
}


dmz::Boolean
dmz::WebServicesPluginObject::_update (const Handle ObjectHandle) {

   Boolean result (False);

   if (!_inUpdate && !_inDump) {

      if (_publishTable.add (ObjectHandle)) {

//         ObjectModule *objMod (get_object_module ());
//         if (objMod) {

//            ObjectType type = objMod->lookup_object_type (ObjectHandle);

//_log.info << "_update: " << ObjectHandle << " : " << type.get_name () << endl;
//         }
      }

      result = True;
   }

   return result;
}


void
dmz::WebServicesPluginObject::_init_filter_list (Config &local) {

   ConfigIterator it;
   Config filter;

   // Read list in reverse order so they are stored in the correct order in the
   // the filter list
   while (local.get_prev_config (it, filter)) {

      FilterStruct *fs (filter.has_children () ? new FilterStruct : 0);
      if (fs) {

         Config objects;

         if (filter.lookup_all_config ("object-type-set.object-type", objects)) {

            _init_object_type_filter (objects, *fs);
         }
         else { _log.info << "No object types filtered" << endl; }

         Config attrConfig;

         if (filter.lookup_all_config ("attribute", attrConfig)) {

            _init_attribute_filter (attrConfig, *fs);
         }

         Config stateConfig;

         if (filter.lookup_all_config ("state", stateConfig)) {

            _init_state_filter (stateConfig, *fs);
         }

         if (!_filterList) {

            _filterList = fs;
         }
         else {

            fs->next = _filterList;
            _filterList = fs;
         }
      }
   }
}


void
dmz::WebServicesPluginObject::_init_object_type_filter (
      Config &objects,
      FilterStruct &filter) {

   RuntimeContext *context (get_plugin_runtime_context ());

   ConfigIterator typesIt;
   Config typeConfig;

   while (objects.get_next_config (typesIt, typeConfig)) {

      const String Name (config_to_string ("name", typeConfig));
      const Boolean Exclude (config_to_boolean ("exclude", typeConfig, True));

      if (Exclude) {

         if (filter.exTypes.add_object_type (Name, context)) {

            _log.info << "Excluding object type: " << Name << endl;
         }
         else {

            _log.error << "Unable to add object type: '" << Name
               << "' to filter" << endl;
         }
      }
      else if (filter.inTypes.add_object_type (Name, context)) {

         _log.info << "Including object type: " << Name << endl;
      }
      else {

         _log.error << "Unable to add object type: '" << Name
            << "' to filter" << endl;
      }
   }
}


void
dmz::WebServicesPluginObject::_init_attribute_filter (
      Config &attrConfig,
      FilterStruct &filter) {

   ConfigIterator attrIt;
   Config currentAttr;

   while (attrConfig.get_next_config (attrIt, currentAttr)) {

      const String Name (
         config_to_string ("name", currentAttr, ObjectAttributeDefaultName));

      const Boolean Contains (
         config_to_boolean ("contains", currentAttr, False));

      const Mask AttrMask (local_config_to_mask (currentAttr, _log));

      if (Contains) {

         FilterAttrStruct *fas (new FilterAttrStruct (Name, AttrMask));

         if (fas) { fas->next = filter.list; filter.list = fas; }
      }
      else {

         const Handle AttrHandle (_defs.create_named_handle (Name));

         Mask *ptr (new Mask (AttrMask));

         if (ptr && !filter.attrTable.store (AttrHandle, ptr)) {

            delete ptr; ptr = 0;

            _log.error << "Unable to store mask for object attribute: "
               << Name << ". Possible duplicate?" << endl;
         }
      }
   }
}


void
dmz::WebServicesPluginObject::_init_state_filter (
      Config &stateConfig,
      FilterStruct &filter) {

   ConfigIterator stateIt;
   Config state;

   while (stateConfig.get_next_config (stateIt, state)) {

      Handle attrHandle (_defaultHandle);

      const String AttrName (config_to_string ("attribute", state));

      if (AttrName) { attrHandle = _defs.create_named_handle (AttrName); }

      Mask stateMask;
      _defs.lookup_state (config_to_string ("name", state), stateMask);

      if (stateMask) {

         Mask *ptr (filter.stateTable.lookup (attrHandle));

         if (ptr) { (*ptr) |= stateMask; }
         else {

            ptr = new Mask (stateMask);

            if (ptr && !filter.stateTable.store (attrHandle, ptr)) {

               delete ptr; ptr = 0;
            }
         }
      }
   }
}


void
dmz::WebServicesPluginObject::_init (Config &local) {

   RuntimeContext *context (get_plugin_runtime_context ());

   _webservicesName = config_to_string ("module-name.web-services", local);

   _defaultHandle = _defs.create_named_handle (ObjectAttributeDefaultName);

   _loginSuccessMsg = config_create_message (
      "message.login-success",
      local,
      "LoginSuccessMessage",
       context);

   _loginFailedMsg = config_create_message (
      "message.login-failed",
      local,
      "LoginFailedMessage",
       context);

   _logoutMsg = config_create_message (
      "message.logout",
      local,
      "LogoutMessage",
       context);

   subscribe_to_message (_loginSuccessMsg);
   subscribe_to_message (_loginFailedMsg);
   subscribe_to_message (_logoutMsg);

   Config filterList;
   if (local.lookup_all_config ("filter", filterList)) {

      _init_filter_list (filterList);
   }

   activate_global_object_observer ();
}


extern "C" {

DMZ_PLUGIN_FACTORY_LINK_SYMBOL dmz::Plugin *
create_dmzWebServicesPluginObject (
      const dmz::PluginInfo &Info,
      dmz::Config &local,
      dmz::Config &global) {

   return new dmz::WebServicesPluginObject (Info, local);
}

};