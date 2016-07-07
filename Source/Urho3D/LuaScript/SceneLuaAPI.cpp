#include "../IO/File.h"
#include "../Resource/ResourceCache.h"
#include "../Scene/Animatable.h"
#include "../Scene/AnimationDefs.h"
#include "../Scene/Component.h"
#include "../Scene/LogicComponent.h"
#include "../Scene/Node.h"
#include "../Scene/ObjectAnimation.h"
#include "../Scene/ReplicationState.h"
#include "../Scene/Scene.h"
#include "../Scene/SceneEvents.h"
#include "../Scene/SceneResolver.h"
#include "../Scene/Serializable.h"
#include "../Scene/SmoothedTransform.h"
#include "../Scene/SplinePath.h"
#include "../Scene/UnknownComponent.h"
#include "../Scene/ValueAnimation.h"
#include "../Scene/ValueAnimationInfo.h"
#include "../LuaScript/LuaFile.h"
#include "../LuaScript/LuaScriptInstance.h"
#include "../LuaScript/LuaScriptUtils.h"

#include <kaguya.hpp>

namespace Urho3D
{

static void RegisterAnimatable(kaguya::State& lua)
{
    using namespace kaguya;

    lua["KAnimatable"].setClass(UserdataMetatable<Animatable, Serializable>()

        .addFunction("LoadXML", &Animatable::LoadXML)
        .addFunction("SaveXML", &Animatable::SaveXML)
        .addFunction("LoadJSON", &Animatable::LoadJSON)
        .addFunction("SaveJSON", &Animatable::SaveJSON)

        .addFunction("SetAnimationEnabled", &Animatable::SetAnimationEnabled)
        .addFunction("SetAnimationTime", &Animatable::SetAnimationTime)
        .addFunction("SetObjectAnimation", &Animatable::SetObjectAnimation)
        .addFunction("SetAttributeAnimation", &Animatable::SetAttributeAnimation)
        .addFunction("SetAttributeAnimationWrapMode", &Animatable::SetAttributeAnimationWrapMode)
        .addFunction("SetAttributeAnimationSpeed", &Animatable::SetAttributeAnimationSpeed)
        .addFunction("SetAttributeAnimationTime", &Animatable::SetAttributeAnimationTime)
        .addFunction("RemoveObjectAnimation", &Animatable::RemoveObjectAnimation)
        .addFunction("RemoveAttributeAnimation", &Animatable::RemoveAttributeAnimation)
        .addFunction("GetAnimationEnabled", &Animatable::GetAnimationEnabled)
        .addFunction("GetObjectAnimation", &Animatable::GetObjectAnimation)
        .addFunction("GetAttributeAnimation", &Animatable::GetAttributeAnimation)
        .addFunction("GetAttributeAnimationWrapMode", &Animatable::GetAttributeAnimationWrapMode)
        .addFunction("GetAttributeAnimationSpeed", &Animatable::GetAttributeAnimationSpeed)
        .addFunction("GetAttributeAnimationTime", &Animatable::GetAttributeAnimationTime)

        .addProperty("animationEnabled", &Animatable::GetAnimationEnabled, &Animatable::SetAnimationEnabled)
        .addProperty("objectAnimation", &Animatable::GetObjectAnimation, &Animatable::SetObjectAnimation)
        .addProperty("animationTime", &Animatable::SetAnimationTime)
    );
}

static void RegisterAnimationDefs(kaguya::State& lua)
{
    using namespace kaguya;

    // enum WrapMode;
    lua["KWM_LOOP"] = WM_LOOP;
    lua["KWM_ONCE"] = WM_ONCE;
    lua["KWM_CLAMP"] = WM_CLAMP;

}

static void RegisterComponent(kaguya::State& lua)
{
    using namespace kaguya;

    lua["KComponent"].setClass(UserdataMetatable<Component, Animatable>()
        .addStaticFunction("new", &KCreateObject<Component>)

        .addFunction("DrawDebugGeometry", &Component::DrawDebugGeometry)

        .addFunction("SetEnabled", &Component::SetEnabled)
        .addFunction("Remove", &Component::Remove)
        .addFunction("GetID", &Component::GetID)
        .addFunction("GetNode", &Component::GetNode)
        .addFunction("GetScene", &Component::GetScene)
        .addFunction("IsEnabled", &Component::IsEnabled)
        .addFunction("IsEnabledEffective", &Component::IsEnabledEffective)

        .addFunction("GetComponent", static_cast<Component* (Component::*)(StringHash) const>(&Component::GetComponent))

        // .addFunction("GetComponents", &Component::GetComponents)

        .addFunction("AddReplicationState", &Component::AddReplicationState)
        .addFunction("PrepareNetworkUpdate", &Component::PrepareNetworkUpdate)
        .addFunction("CleanupConnection", &Component::CleanupConnection)

        .addProperty("id", &Component::GetID)
        .addProperty("node", &Component::GetNode)
        .addProperty("scene", &Component::GetScene)
        .addProperty("enabled", &Component::IsEnabled, &Component::SetEnabled)
        .addProperty("enabledEffective", &Component::IsEnabledEffective)
    );
}

static void RegisterLogicComponent(kaguya::State& lua)
{
    using namespace kaguya;

    lua["KUSE_UPDATE"] = USE_UPDATE;
    lua["KUSE_POSTUPDATE"] = USE_POSTUPDATE;
    lua["KUSE_FIXEDUPDATE"] = USE_FIXEDUPDATE;
    lua["KUSE_FIXEDPOSTUPDATE"] = USE_FIXEDPOSTUPDATE;

    lua["KLogicComponent"].setClass(UserdataMetatable<LogicComponent, Component>()
        .addStaticFunction("new", &KCreateObject<LogicComponent>)

        .addFunction("SetUpdateEventMask", &LogicComponent::SetUpdateEventMask)
        .addFunction("GetUpdateEventMask", &LogicComponent::GetUpdateEventMask)
        .addFunction("IsDelayedStartCalled", &LogicComponent::IsDelayedStartCalled)

        .addProperty("updateEventMask", &LogicComponent::GetUpdateEventMask, &LogicComponent::SetUpdateEventMask)
        .addProperty("delayedStartCalled", &LogicComponent::IsDelayedStartCalled)
    );
}

static SharedPtr<Component> NodeCreateComponent(Node* node, const char* type, CreateMode mode = REPLICATED, unsigned id = 0)
{
    return SharedPtr<Component>(node->CreateComponent(StringHash(type), mode, id));
}

static SharedPtr<Component> NodeGetOrCreateComponent(Node* node, const char* type, CreateMode mode = REPLICATED, unsigned id = 0)
{
    return SharedPtr<Component>(node->GetOrCreateComponent(StringHash(type), mode, id));
}

static SharedPtr<Component> NodeGetComponent(const Node* node, const char* type, bool recursive = false)
{
    return SharedPtr<Component>(node->GetComponent(StringHash(type), recursive));
}

static kaguya::LuaTable NodeCreateScriptObject1(Node* node, const char* scriptObjectType)
{
    LuaScriptInstance* instance = node->CreateComponent<LuaScriptInstance>();    
    instance->CreateObject(scriptObjectType);

    // Push Script object to stack
    lua_rawgeti(instance->GetLuaState(), LUA_REGISTRYINDEX, instance->GetScriptObjectRef());
    
    // return Lua table Object.
    return kaguya::LuaTable(instance->GetLuaState(), kaguya::StackTop());
}

static kaguya::LuaTable NodeCreateScriptObject2(Node* node, const char* fileName, const char* scriptObjectType)
{
    ResourceCache* cache = node->GetSubsystem<ResourceCache>();
    LuaFile* scriptFile = cache->GetResource<LuaFile>(fileName);
    if (!scriptFile)
    {
        return 0;
    }

    LuaScriptInstance* instance = node->CreateComponent<LuaScriptInstance>();
    instance->CreateObject(scriptFile, scriptObjectType);

    lua_rawgeti(instance->GetLuaState(), LUA_REGISTRYINDEX, instance->GetScriptObjectRef());
    return kaguya::LuaTable(instance->GetLuaState(), kaguya::StackTop());
}

static void RegisterNode(kaguya::State& lua)
{
    using namespace kaguya;

    // enum CreateMode;
    lua["KREPLICATED"] = REPLICATED;
    lua["KLOCAL"] = LOCAL;

    // enum TransformSpace;
    lua["KTS_LOCAL"] = TS_LOCAL;
    lua["KTS_PARENT"] = TS_PARENT;
    lua["KTS_WORLD"] = TS_WORLD;

    lua["KNode"].setClass(UserdataMetatable<Node, Animatable>()
        .addStaticFunction("new", &KCreateObject<Node>)

        .addFunction("SetName", &Node::SetName)
        .addFunction("SetTags", &Node::SetTags)
        .addFunction("AddTag", &Node::AddTag)

        .addOverloadedFunctions("AddTags",
            static_cast<void(Node::*)(const String&, char)>(&Node::AddTags),
            static_cast<void(Node::*)(const StringVector&)>(&Node::AddTags))

        .addFunction("RemoveTag", &Node::RemoveTag)
        .addFunction("RemoveAllTags", &Node::RemoveAllTags)
        .addFunction("SetPosition", &Node::SetPosition)

        .addOverloadedFunctions("SetPosition2D",
            static_cast<void(Node::*)(const Vector2&)>(&Node::SetPosition2D),
            static_cast<void(Node::*)(float, float)>(&Node::SetPosition2D))

        .addFunction("SetRotation", &Node::SetRotation)
        .addFunction("SetRotation2D", &Node::SetRotation2D)
        .addFunction("SetDirection", &Node::SetDirection)

        .addOverloadedFunctions("SetScale",
            static_cast<void(Node::*)(float)>(&Node::SetScale),
            static_cast<void(Node::*)(const Vector3&)>(&Node::SetScale))

        .addOverloadedFunctions("SetScale2D",
            static_cast<void(Node::*)(const Vector2&)>(&Node::SetScale2D),
            static_cast<void(Node::*)(float, float)>(&Node::SetScale2D))

        .addOverloadedFunctions("SetTransform",
            static_cast<void(Node::*)(const Vector3&, const Quaternion&)>(&Node::SetTransform),
            static_cast<void(Node::*)(const Vector3&, const Quaternion&, float)>(&Node::SetTransform),
            static_cast<void(Node::*)(const Vector3&, const Quaternion&, const Vector3&)>(&Node::SetTransform))

        .addOverloadedFunctions("SetTransform2D",
            static_cast<void(Node::*)(const Vector2&, float)>(&Node::SetTransform2D),
            static_cast<void(Node::*)(const Vector2&, float, float)>(&Node::SetTransform2D),
            static_cast<void(Node::*)(const Vector2&, float, const Vector2&)>(&Node::SetTransform2D))

        .addFunction("SetWorldPosition", &Node::SetWorldPosition)

        .addOverloadedFunctions("SetWorldPosition2D",
            static_cast<void(Node::*)(const Vector2&)>(&Node::SetWorldPosition2D),
            static_cast<void(Node::*)(float, float)>(&Node::SetWorldPosition2D))

        .addFunction("SetWorldRotation", &Node::SetWorldRotation)
        .addFunction("SetWorldRotation2D", &Node::SetWorldRotation2D)
        .addFunction("SetWorldDirection", &Node::SetWorldDirection)

        .addOverloadedFunctions("SetWorldScale",
            static_cast<void(Node::*)(float)>(&Node::SetWorldScale),
            static_cast<void(Node::*)(const Vector3&)>(&Node::SetWorldScale))

        .addOverloadedFunctions("SetWorldScale2D",
            static_cast<void(Node::*)(const Vector2&)>(&Node::SetWorldScale2D),
            static_cast<void(Node::*)(float, float)>(&Node::SetWorldScale2D))

        .addOverloadedFunctions("SetWorldTransform",
            static_cast<void(Node::*)(const Vector3&, const Quaternion&)>(&Node::SetWorldTransform),
            static_cast<void(Node::*)(const Vector3&, const Quaternion&, float)>(&Node::SetWorldTransform),
            static_cast<void(Node::*)(const Vector3&, const Quaternion&, const Vector3&)>(&Node::SetWorldTransform))

        .addOverloadedFunctions("SetWorldTransform2D",
            static_cast<void(Node::*)(const Vector2&, float)>(&Node::SetWorldTransform2D),
            static_cast<void(Node::*)(const Vector2&, float, float)>(&Node::SetWorldTransform2D),
            static_cast<void(Node::*)(const Vector2&, float, const Vector2&)>(&Node::SetWorldTransform2D))

        .addFunction("Translate", &Node::Translate)
        .addFunction("Translate2D", &Node::Translate2D)
        .addFunction("Rotate", &Node::Rotate)
        .addFunction("Rotate2D", &Node::Rotate2D)
        .addFunction("RotateAround", &Node::RotateAround)
        .addFunction("RotateAround2D", &Node::RotateAround2D)
        .addFunction("Pitch", &Node::Pitch)
        .addFunction("Yaw", &Node::Yaw)
        .addFunction("Roll", &Node::Roll)
        .addFunction("LookAt", &Node::LookAt)

        .addOverloadedFunctions("Scale",
            static_cast<void(Node::*)(float)>(&Node::Scale),
            static_cast<void(Node::*)(const Vector3&)>(&Node::Scale))

        .addFunction("Scale2D", &Node::Scale2D)

        .addFunction("SetEnabled", static_cast<void(Node::*)(bool)>(&Node::SetEnabled))

        .addFunction("SetDeepEnabled", &Node::SetDeepEnabled)
        .addFunction("ResetDeepEnabled", &Node::ResetDeepEnabled)
        .addFunction("SetEnabledRecursive", &Node::SetEnabledRecursive)
        .addFunction("SetOwner", &Node::SetOwner)
        .addFunction("MarkDirty", &Node::MarkDirty)

        .addFunction("CreateChild", static_cast<Node*(Node::*)(const String&, CreateMode, unsigned)>(&Node::CreateChild))

        .addFunction("AddChild", &Node::AddChild)
        .addFunction("RemoveChild", static_cast<void(Node::*)(Node*)>(&Node::RemoveChild))
        .addFunction("RemoveAllChildren", &Node::RemoveAllChildren)
        .addFunction("RemoveChildren", &Node::RemoveChildren)
        
        .addStaticFunction("CreateComponent", &NodeCreateComponent)
        .addStaticFunction("GetOrCreateComponent", &NodeGetOrCreateComponent)

        .addStaticFunction("CreateScriptObject", &NodeCreateScriptObject1)
        .addStaticFunction("CreateScriptObject2", &NodeCreateScriptObject2)

        .addOverloadedFunctions("CloneComponent",
            static_cast<Component*(Node::*)(Component*, unsigned)>(&Node::CloneComponent),
            static_cast<Component*(Node::*)(Component*, CreateMode, unsigned)>(&Node::CloneComponent))

        .addOverloadedFunctions("RemoveComponent",
            static_cast<void(Node::*)(Component*)>(&Node::RemoveComponent),
            static_cast<void(Node::*)(StringHash)>(&Node::RemoveComponent))


        .addOverloadedFunctions("RemoveComponents",
            static_cast<void(Node::*)(bool, bool)>(&Node::RemoveComponents),
            static_cast<void(Node::*)(StringHash)>(&Node::RemoveComponents))

        .addFunction("RemoveAllComponents", &Node::RemoveAllComponents)
        .addFunction("Clone", &Node::Clone)
        .addFunction("Remove", &Node::Remove)
        .addFunction("SetParent", &Node::SetParent)
        .addFunction("SetVar", &Node::SetVar)
        .addFunction("AddListener", &Node::AddListener)
        .addFunction("RemoveListener", &Node::RemoveListener)
        .addFunction("GetID", &Node::GetID)
        .addFunction("GetName", &Node::GetName)
        .addFunction("GetNameHash", &Node::GetNameHash)
        .addFunction("GetTags", &Node::GetTags)
        .addFunction("HasTag", &Node::HasTag)
        .addFunction("GetParent", &Node::GetParent)
        .addFunction("GetScene", &Node::GetScene)
        .addFunction("IsEnabled", &Node::IsEnabled)
        .addFunction("IsEnabledSelf", &Node::IsEnabledSelf)
        .addFunction("GetOwner", &Node::GetOwner)
        .addFunction("GetPosition", &Node::GetPosition)
        .addFunction("GetPosition2D", &Node::GetPosition2D)
        .addFunction("GetRotation", &Node::GetRotation)
        .addFunction("GetRotation2D", &Node::GetRotation2D)
        .addFunction("GetDirection", &Node::GetDirection)
        .addFunction("GetUp", &Node::GetUp)
        .addFunction("GetRight", &Node::GetRight)
        .addFunction("GetScale", &Node::GetScale)
        .addFunction("GetScale2D", &Node::GetScale2D)
        .addFunction("GetTransform", &Node::GetTransform)
        .addFunction("GetWorldPosition", &Node::GetWorldPosition)
        .addFunction("GetWorldPosition2D", &Node::GetWorldPosition2D)
        .addFunction("GetWorldRotation", &Node::GetWorldRotation)
        .addFunction("GetWorldRotation2D", &Node::GetWorldRotation2D)
        .addFunction("GetWorldDirection", &Node::GetWorldDirection)
        .addFunction("GetWorldUp", &Node::GetWorldUp)
        .addFunction("GetWorldRight", &Node::GetWorldRight)
        .addFunction("GetWorldScale", &Node::GetWorldScale)
        .addFunction("GetWorldScale2D", &Node::GetWorldScale2D)
        .addFunction("GetWorldTransform", &Node::GetWorldTransform)

        .addOverloadedFunctions("LocalToWorld",
            static_cast<Vector3(Node::*)(const Vector3&) const>(&Node::LocalToWorld),
            static_cast<Vector3(Node::*)(const Vector4&) const>(&Node::LocalToWorld))

        .addFunction("LocalToWorld2D", &Node::LocalToWorld2D)

        .addOverloadedFunctions("WorldToLocal",
            static_cast<Vector3(Node::*)(const Vector3&) const>(&Node::WorldToLocal),
            static_cast<Vector3(Node::*)(const Vector4&) const>(&Node::WorldToLocal))

        .addFunction("WorldToLocal2D", &Node::WorldToLocal2D)
        .addFunction("IsDirty", &Node::IsDirty)
        .addFunction("GetNumChildren", &Node::GetNumChildren)

        .addOverloadedFunctions("GetChildren",
            static_cast<const Vector<SharedPtr<Node> >&(Node::*)() const>(&Node::GetChildren),
            static_cast<void(Node::*)(PODVector<Node*>&, bool) const>(&Node::GetChildren))

        /*
        .addFunction("GetChildrenWithComponent", &Node::GetChildrenWithComponent)
        .addFunction("GetChildrenWithTag", &Node::GetChildrenWithTag)
        */

        .addOverloadedFunctions("GetChild",
            static_cast<Node*(Node::*)(unsigned) const>(&Node::GetChild),
            static_cast<Node*(Node::*)(const String&, bool) const>(&Node::GetChild),
            static_cast<Node*(Node::*)(const char*, bool) const>(&Node::GetChild),
            static_cast<Node*(Node::*)(StringHash, bool) const>(&Node::GetChild))

        .addFunction("GetNumComponents", &Node::GetNumComponents)
        .addFunction("GetNumNetworkComponents", &Node::GetNumNetworkComponents)

        .addOverloadedFunctions("GetComponents",
            static_cast<const Vector<SharedPtr<Component> >&(Node::*)() const>(&Node::GetComponents),
            static_cast<void(Node::*)(PODVector<Component*>&, StringHash, bool) const>(&Node::GetComponents))

        .addStaticFunction("GetComponent", &NodeGetComponent)
        // .addFunction("GetParentComponent", &Node::GetParentComponent)
        // .addFunction("HasComponent", &Node::HasComponent)

        .addFunction("GetListeners", &Node::GetListeners)
        .addFunction("GetVar", &Node::GetVar)
        .addFunction("GetVars", &Node::GetVars)
        .addFunction("SetID", &Node::SetID)
        .addFunction("SetScene", &Node::SetScene)
        .addFunction("ResetScene", &Node::ResetScene)
        .addFunction("GetDependencyNodes", &Node::GetDependencyNodes)
        .addFunction("PrepareNetworkUpdate", &Node::PrepareNetworkUpdate)
        .addFunction("CleanupConnection", &Node::CleanupConnection)
        .addFunction("MarkReplicationDirty", &Node::MarkReplicationDirty)
        .addFunction("AddComponent", &Node::AddComponent)
        .addFunction("GetNumPersistentChildren", &Node::GetNumPersistentChildren)
        .addFunction("GetNumPersistentComponents", &Node::GetNumPersistentComponents)
        .addFunction("SetPositionSilent", &Node::SetPositionSilent)
        .addFunction("SetRotationSilent", &Node::SetRotationSilent)
        .addFunction("SetScaleSilent", &Node::SetScaleSilent)
        .addFunction("SetTransformSilent", &Node::SetTransformSilent)
        .addProperty("iD", &Node::GetID, &Node::SetID)
        .addProperty("name", &Node::GetName, &Node::SetName)
        .addProperty("nameHash", &Node::GetNameHash)
        .addProperty("tags", &Node::GetTags, &Node::SetTags)
        .addProperty("parent", &Node::GetParent, &Node::SetParent)
        .addProperty("scene", &Node::GetScene, &Node::SetScene)
        .addProperty("enabled", &Node::IsEnabled)
        .addProperty("enabledSelf", &Node::IsEnabledSelf)
        .addProperty("owner", &Node::GetOwner, &Node::SetOwner)
        .addProperty("position", &Node::GetPosition, &Node::SetPosition)
        .addProperty("position2D", &Node::GetPosition2D)
        .addProperty("rotation", &Node::GetRotation, &Node::SetRotation)
        .addProperty("rotation2D", &Node::GetRotation2D, &Node::SetRotation2D)
        .addProperty("direction", &Node::GetDirection, &Node::SetDirection)
        .addProperty("up", &Node::GetUp)
        .addProperty("right", &Node::GetRight)
        .addProperty("scale", &Node::GetScale, static_cast<void(Node::*)(const Vector3&)>(&Node::SetScale))
        .addProperty("scale2D", &Node::GetScale2D, static_cast<void(Node::*)(const Vector2&)>(&Node::SetScale2D))
        .addProperty("transform", &Node::GetTransform)
        .addProperty("worldPosition", &Node::GetWorldPosition, &Node::SetWorldPosition)
        .addProperty("worldPosition2D", &Node::GetWorldPosition2D)
        .addProperty("worldRotation", &Node::GetWorldRotation, &Node::SetWorldRotation)
        .addProperty("worldRotation2D", &Node::GetWorldRotation2D, &Node::SetWorldRotation2D)
        .addProperty("worldDirection", &Node::GetWorldDirection, &Node::SetWorldDirection)
        .addProperty("worldUp", &Node::GetWorldUp)
        .addProperty("worldRight", &Node::GetWorldRight)
        .addProperty("worldScale", &Node::GetWorldScale)
        .addProperty("worldScale2D", &Node::GetWorldScale2D)
        .addProperty("worldTransform", &Node::GetWorldTransform)
        .addProperty("dirty", &Node::IsDirty)
        .addProperty("numComponents", &Node::GetNumComponents)
        .addProperty("numNetworkComponents", &Node::GetNumNetworkComponents)
        .addProperty("listeners", &Node::GetListeners)
        .addProperty("vars", &Node::GetVars)
        .addProperty("dependencyNodes", &Node::GetDependencyNodes)
        .addProperty("numPersistentChildren", &Node::GetNumPersistentChildren)
        .addProperty("numPersistentComponents", &Node::GetNumPersistentComponents)
        .addProperty("deepEnabled", &Node::SetDeepEnabled)
        .addProperty("enabledRecursive", &Node::SetEnabledRecursive)
        .addProperty("positionSilent", &Node::SetPositionSilent)
        .addProperty("rotationSilent", &Node::SetRotationSilent)
        .addProperty("scaleSilent", &Node::SetScaleSilent)
    );
}

static void RegisterObjectAnimation(kaguya::State& lua)
{
    using namespace kaguya;

    lua["KObjectAnimation"].setClass(UserdataMetatable<ObjectAnimation, Resource>()
        .addStaticFunction("new", &KCreateObject<ObjectAnimation>)

        .addFunction("AddAttributeAnimation", &ObjectAnimation::AddAttributeAnimation)

        .addOverloadedFunctions("RemoveAttributeAnimation",
            static_cast<void(ObjectAnimation::*)(const String&)>(&ObjectAnimation::RemoveAttributeAnimation),
            static_cast<void(ObjectAnimation::*)(ValueAnimation*)>(&ObjectAnimation::RemoveAttributeAnimation))

        .addFunction("GetAttributeAnimation", &ObjectAnimation::GetAttributeAnimation)
        .addFunction("GetAttributeAnimationWrapMode", &ObjectAnimation::GetAttributeAnimationWrapMode)
        .addFunction("GetAttributeAnimationSpeed", &ObjectAnimation::GetAttributeAnimationSpeed)
        // .addFunction("GetAttributeAnimationInfos", &ObjectAnimation::GetAttributeAnimationInfos)
        .addFunction("GetAttributeAnimationInfo", &ObjectAnimation::GetAttributeAnimationInfo)
    );
}

//static void RegisterReplicationState(kaguya::State& lua)
//{
//    using namespace kaguya;
//
//    lua["KMAX_NETWORK_ATTRIBUTES"] = MAX_NETWORK_ATTRIBUTES;
//    lua["KDirtyBits"].setClass(UserdataMetatable<DirtyBits>()
//        .setConstructors<DirtyBits(),
//        DirtyBits(const DirtyBits&)()
//
//        .addFunction("Set", &DirtyBits::Set)
//        .addFunction("Clear", &DirtyBits::Clear)
//        .addFunction("ClearAll", &DirtyBits::ClearAll)
//        .addFunction("IsSet", &DirtyBits::IsSet)
//        .addFunction("Count", &DirtyBits::Count)
//
//        .addProperty("", &DirtyBits::Set)
//        .addProperty("data", &DirtyBits::data_)
//        .addProperty("count", &DirtyBits::count_)
//        );
//    lua["KNetworkState"].setClass(UserdataMetatable<NetworkState>()
//        .setConstructors<NetworkState()()
//
//        .addProperty("attributes", &NetworkState::attributes_)
//        .addProperty("currentValues", &NetworkState::currentValues_)
//        .addProperty("previousValues", &NetworkState::previousValues_)
//        .addProperty("replicationStates", &NetworkState::replicationStates_)
//        .addProperty("previousVars", &NetworkState::previousVars_)
//        .addProperty("interceptMask", &NetworkState::interceptMask_)
//        );
//    lua["KReplicationState"].setClass(UserdataMetatable<ReplicationState>()
//
//        .addProperty("connection", &ReplicationState::connection_)
//        );
//    lua["KComponentReplicationState"].setClass(UserdataMetatable<ComponentReplicationState, ReplicationState>()
//
//        .addProperty("nodeState", &ComponentReplicationState::nodeState_)
//        .addProperty("component", &ComponentReplicationState::component_)
//        .addProperty("dirtyAttributes", &ComponentReplicationState::dirtyAttributes_)
//        );
//    lua["KNodeReplicationState"].setClass(UserdataMetatable<NodeReplicationState, ReplicationState>()
//        .setConstructors<NodeReplicationState()()
//
//        .addProperty("sceneState", &NodeReplicationState::sceneState_)
//        .addProperty("node", &NodeReplicationState::node_)
//        .addProperty("dirtyAttributes", &NodeReplicationState::dirtyAttributes_)
//        .addProperty("dirtyVars", &NodeReplicationState::dirtyVars_)
//        .addProperty("componentStates", &NodeReplicationState::componentStates_)
//        .addProperty("priorityAcc", &NodeReplicationState::priorityAcc_)
//        .addProperty("markedDirty", &NodeReplicationState::markedDirty_)
//        );
//    lua["KSceneReplicationState"].setClass(UserdataMetatable<SceneReplicationState, ReplicationState>()
//
//        .addFunction("Clear", &SceneReplicationState::Clear)
//        .addProperty("nodeStates", &SceneReplicationState::nodeStates_)
//        .addProperty("dirtyNodes", &SceneReplicationState::dirtyNodes_)
//        );
//}

static bool SceneLoadXML(Scene* scene, const char* filepath)
{
    SharedPtr<File> file(new File(globalContext, filepath));
    if (!file->IsOpen())
        return false;
    return scene->LoadXML(*file);
}

static bool SceneLoadJSON(Scene* scene, const char* filepath)
{
    SharedPtr<File> file(new File(globalContext, filepath));
    if (!file->IsOpen())
        return false;
    return scene->LoadJSON(*file);
}

static bool SceneSaveXML(const Scene* scene, const char* filepath, const String& indentation = "\t")
{
    SharedPtr<File> file(new File(globalContext, filepath, FILE_WRITE));
    if (!file->IsOpen())
        return false;
    return scene->SaveXML(*file, indentation);
}

static bool SceneSaveJSON(const Scene* scene, const char* filepath, const String& indentation = "\t")
{
    SharedPtr<File> file(new File(globalContext, filepath, FILE_WRITE));
    if (!file->IsOpen())
        return false;
    return scene->SaveJSON(*file, indentation);
}

static void RegisterScene(kaguya::State& lua)
{
    using namespace kaguya;

    lua["KFIRST_REPLICATED_ID"] = FIRST_REPLICATED_ID;
    lua["KLAST_REPLICATED_ID"] = LAST_REPLICATED_ID;
    lua["KFIRST_LOCAL_ID"] = FIRST_LOCAL_ID;
    lua["KLAST_LOCAL_ID"] = LAST_LOCAL_ID;

    // enum LoadMode;
    lua["KLOAD_RESOURCES_ONLY"] = LOAD_RESOURCES_ONLY;
    lua["KLOAD_SCENE"] = LOAD_SCENE;
    lua["KLOAD_SCENE_AND_RESOURCES"] = LOAD_SCENE_AND_RESOURCES;

    lua["KScene"].setClass(UserdataMetatable<Scene, Node>()
        .addStaticFunction("new", &KCreateObject<Scene>)

        .addStaticFunction("LoadXML", &SceneLoadXML)
        .addStaticFunction("LoadJSON", &SceneLoadJSON)
        .addStaticFunction("SaveXML", &SceneSaveXML)
        .addStaticFunction("SaveJSON", &SceneSaveJSON)
        
        // .addFunction("Instantiate", &Scene::Instantiate)

        .addFunction("Clear", &Scene::Clear)

        .addFunction("SetUpdateEnabled", &Scene::SetUpdateEnabled)
        .addFunction("SetTimeScale", &Scene::SetTimeScale)
        .addFunction("SetElapsedTime", &Scene::SetElapsedTime)
        .addFunction("SetSmoothingConstant", &Scene::SetSmoothingConstant)
        .addFunction("SetSnapThreshold", &Scene::SetSnapThreshold)
        .addFunction("SetAsyncLoadingMs", &Scene::SetAsyncLoadingMs)

        // .addFunction("AddRequiredPackageFile", &Scene::AddRequiredPackageFile)
        // .addFunction("ClearRequiredPackageFiles", &Scene::ClearRequiredPackageFiles)

        .addFunction("RegisterVar", &Scene::RegisterVar)
        .addFunction("UnregisterVar", &Scene::UnregisterVar)
        .addFunction("UnregisterAllVars", &Scene::UnregisterAllVars)
        .addFunction("GetNode", &Scene::GetNode)
        // .addFunction("GetComponent", &Scene::GetComponent)
        .addFunction("GetNodesWithTag", &Scene::GetNodesWithTag)
        .addFunction("IsUpdateEnabled", &Scene::IsUpdateEnabled)
        .addFunction("IsAsyncLoading", &Scene::IsAsyncLoading)
        .addFunction("GetAsyncProgress", &Scene::GetAsyncProgress)
        .addFunction("GetAsyncLoadMode", &Scene::GetAsyncLoadMode)
        .addFunction("GetFileName", &Scene::GetFileName)
        .addFunction("GetChecksum", &Scene::GetChecksum)
        .addFunction("GetTimeScale", &Scene::GetTimeScale)
        .addFunction("GetElapsedTime", &Scene::GetElapsedTime)
        .addFunction("GetSmoothingConstant", &Scene::GetSmoothingConstant)
        .addFunction("GetSnapThreshold", &Scene::GetSnapThreshold)
        .addFunction("GetAsyncLoadingMs", &Scene::GetAsyncLoadingMs)
        // .addFunction("GetRequiredPackageFiles", &Scene::GetRequiredPackageFiles)
        .addFunction("GetVarName", &Scene::GetVarName)
        .addFunction("Update", &Scene::Update)
        .addFunction("BeginThreadedUpdate", &Scene::BeginThreadedUpdate)
        .addFunction("EndThreadedUpdate", &Scene::EndThreadedUpdate)
        .addFunction("DelayedMarkedDirty", &Scene::DelayedMarkedDirty)
        .addFunction("IsThreadedUpdate", &Scene::IsThreadedUpdate)
        .addFunction("GetFreeNodeID", &Scene::GetFreeNodeID)
        .addFunction("GetFreeComponentID", &Scene::GetFreeComponentID)
        .addFunction("NodeTagAdded", &Scene::NodeTagAdded)
        .addFunction("NodeTagRemoved", &Scene::NodeTagRemoved)
        .addFunction("NodeAdded", &Scene::NodeAdded)
        .addFunction("NodeRemoved", &Scene::NodeRemoved)
        .addFunction("ComponentAdded", &Scene::ComponentAdded)
        .addFunction("ComponentRemoved", &Scene::ComponentRemoved)
        .addFunction("PrepareNetworkUpdate", &Scene::PrepareNetworkUpdate)
        .addFunction("CleanupConnection", &Scene::CleanupConnection)
        .addFunction("MarkReplicationDirty", &Scene::MarkReplicationDirty)
        .addProperty("updateEnabled", &Scene::IsUpdateEnabled, &Scene::SetUpdateEnabled)
        .addProperty("asyncLoading", &Scene::IsAsyncLoading)
        .addProperty("fileName", &Scene::GetFileName)
        .addProperty("checksum", &Scene::GetChecksum)
        .addProperty("timeScale", &Scene::GetTimeScale, &Scene::SetTimeScale)
        .addProperty("elapsedTime", &Scene::GetElapsedTime, &Scene::SetElapsedTime)
        .addProperty("smoothingConstant", &Scene::GetSmoothingConstant, &Scene::SetSmoothingConstant)
        .addProperty("snapThreshold", &Scene::GetSnapThreshold, &Scene::SetSnapThreshold)
        .addProperty("asyncLoadingMs", &Scene::GetAsyncLoadingMs, &Scene::SetAsyncLoadingMs)
        .addProperty("threadedUpdate", &Scene::IsThreadedUpdate)
        );
}

static void RegisterSceneEvents(kaguya::State& lua)
{
    using namespace kaguya;

    lua["KE_SCENEUPDATE"] = E_SCENEUPDATE;
    lua["KE_SCENESUBSYSTEMUPDATE"] = E_SCENESUBSYSTEMUPDATE;
    lua["KE_UPDATESMOOTHING"] = E_UPDATESMOOTHING;
    lua["KE_SCENEDRAWABLEUPDATEFINISHED"] = E_SCENEDRAWABLEUPDATEFINISHED;
    lua["KE_TARGETPOSITION"] = E_TARGETPOSITION;
    lua["KE_TARGETROTATION"] = E_TARGETROTATION;
    lua["KE_ATTRIBUTEANIMATIONUPDATE"] = E_ATTRIBUTEANIMATIONUPDATE;
    lua["KE_ATTRIBUTEANIMATIONADDED"] = E_ATTRIBUTEANIMATIONADDED;
    lua["KE_ATTRIBUTEANIMATIONREMOVED"] = E_ATTRIBUTEANIMATIONREMOVED;
    lua["KE_SCENEPOSTUPDATE"] = E_SCENEPOSTUPDATE;
    lua["KE_ASYNCLOADPROGRESS"] = E_ASYNCLOADPROGRESS;
    lua["KE_ASYNCLOADFINISHED"] = E_ASYNCLOADFINISHED;
    lua["KE_NODEADDED"] = E_NODEADDED;
    lua["KE_NODEREMOVED"] = E_NODEREMOVED;
    lua["KE_COMPONENTADDED"] = E_COMPONENTADDED;
    lua["KE_COMPONENTREMOVED"] = E_COMPONENTREMOVED;
    lua["KE_NODENAMECHANGED"] = E_NODENAMECHANGED;
    lua["KE_NODEENABLEDCHANGED"] = E_NODEENABLEDCHANGED;
    lua["KE_NODETAGADDED"] = E_NODETAGADDED;
    lua["KE_NODETAGREMOVED"] = E_NODETAGREMOVED;
    lua["KE_COMPONENTENABLEDCHANGED"] = E_COMPONENTENABLEDCHANGED;
    lua["KE_TEMPORARYCHANGED"] = E_TEMPORARYCHANGED;
    lua["KE_NODECLONED"] = E_NODECLONED;
    lua["KE_COMPONENTCLONED"] = E_COMPONENTCLONED;
    lua["KE_INTERCEPTNETWORKUPDATE"] = E_INTERCEPTNETWORKUPDATE;
}

static void RegisterSerializable(kaguya::State& lua)
{
    using namespace kaguya;

    lua["KSerializable"].setClass(UserdataMetatable<Serializable, Object>()

        /*
        .addFunction("Load", &Serializable::Load)
        .addFunction("Save", &Serializable::Save)
        .addFunction("LoadXML", &Serializable::LoadXML)
        .addFunction("SaveXML", &Serializable::SaveXML)
        .addFunction("LoadJSON", &Serializable::LoadJSON)
        .addFunction("SaveJSON", &Serializable::SaveJSON)
        */

        .addOverloadedFunctions("SetAttribute",
            static_cast<bool(Serializable::*)(unsigned, const Variant&)>(&Serializable::SetAttribute),
            static_cast<bool(Serializable::*)(const String&, const Variant&)>(&Serializable::SetAttribute))

        .addFunction("ResetToDefault", &Serializable::ResetToDefault)
        .addFunction("RemoveInstanceDefault", &Serializable::RemoveInstanceDefault)
        .addFunction("SetTemporary", &Serializable::SetTemporary)
        .addFunction("SetInterceptNetworkUpdate", &Serializable::SetInterceptNetworkUpdate)
        .addFunction("AllocateNetworkState", &Serializable::AllocateNetworkState)
        .addFunction("WriteInitialDeltaUpdate", &Serializable::WriteInitialDeltaUpdate)
        .addFunction("WriteDeltaUpdate", &Serializable::WriteDeltaUpdate)
        .addFunction("WriteLatestDataUpdate", &Serializable::WriteLatestDataUpdate)
        .addFunction("ReadDeltaUpdate", &Serializable::ReadDeltaUpdate)
        .addFunction("ReadLatestDataUpdate", &Serializable::ReadLatestDataUpdate)

        .addOverloadedFunctions("GetAttribute",
            static_cast<Variant(Serializable::*)(unsigned) const>(&Serializable::GetAttribute),
            static_cast<Variant(Serializable::*)(const String&) const>(&Serializable::GetAttribute))


        .addOverloadedFunctions("GetAttributeDefault",
            static_cast<Variant(Serializable::*)(unsigned) const>(&Serializable::GetAttributeDefault),
            static_cast<Variant(Serializable::*)(const String&) const>(&Serializable::GetAttributeDefault))

        .addFunction("GetNumAttributes", &Serializable::GetNumAttributes)
        .addFunction("GetNumNetworkAttributes", &Serializable::GetNumNetworkAttributes)
        .addFunction("IsTemporary", &Serializable::IsTemporary)
        .addFunction("GetInterceptNetworkUpdate", &Serializable::GetInterceptNetworkUpdate)
        .addFunction("GetNetworkState", &Serializable::GetNetworkState)
    );
}

static void RegisterSmoothedTransform(kaguya::State& lua)
{
    using namespace kaguya;

    lua["KSMOOTH_NONE"] = SMOOTH_NONE;
    lua["KSMOOTH_POSITION"] = SMOOTH_POSITION;
    lua["KSMOOTH_ROTATION"] = SMOOTH_ROTATION;

    lua["KSmoothedTransform"].setClass(UserdataMetatable<SmoothedTransform, Component>()
        .addStaticFunction("new", &KCreateObject<SmoothedTransform>)

        .addFunction("SetTargetPosition", &SmoothedTransform::SetTargetPosition)
        .addFunction("SetTargetRotation", &SmoothedTransform::SetTargetRotation)
        .addFunction("SetTargetWorldPosition", &SmoothedTransform::SetTargetWorldPosition)
        .addFunction("SetTargetWorldRotation", &SmoothedTransform::SetTargetWorldRotation)
        .addFunction("GetTargetPosition", &SmoothedTransform::GetTargetPosition)
        .addFunction("GetTargetRotation", &SmoothedTransform::GetTargetRotation)
        .addFunction("GetTargetWorldPosition", &SmoothedTransform::GetTargetWorldPosition)
        .addFunction("GetTargetWorldRotation", &SmoothedTransform::GetTargetWorldRotation)
        .addFunction("IsInProgress", &SmoothedTransform::IsInProgress)

        .addProperty("targetPosition", &SmoothedTransform::GetTargetPosition, &SmoothedTransform::SetTargetPosition)
        .addProperty("targetRotation", &SmoothedTransform::GetTargetRotation, &SmoothedTransform::SetTargetRotation)
        .addProperty("targetWorldPosition", &SmoothedTransform::GetTargetWorldPosition, &SmoothedTransform::SetTargetWorldPosition)
        .addProperty("targetWorldRotation", &SmoothedTransform::GetTargetWorldRotation, &SmoothedTransform::SetTargetWorldRotation)
        .addProperty("inProgress", &SmoothedTransform::IsInProgress)
    );
}

static void RegisterSplinePath(kaguya::State& lua)
{
    using namespace kaguya;

    lua["KSplinePath"].setClass(UserdataMetatable<SplinePath, Component>()
        .addStaticFunction("new", &KCreateObject<SplinePath>)

        .addFunction("AddControlPoint", &SplinePath::AddControlPoint)
        .addFunction("RemoveControlPoint", &SplinePath::RemoveControlPoint)
        .addFunction("ClearControlPoints", &SplinePath::ClearControlPoints)
        .addFunction("SetInterpolationMode", &SplinePath::SetInterpolationMode)
        .addFunction("SetSpeed", &SplinePath::SetSpeed)
        .addFunction("SetPosition", &SplinePath::SetPosition)
        .addFunction("SetControlledNode", &SplinePath::SetControlledNode)
        .addFunction("GetInterpolationMode", &SplinePath::GetInterpolationMode)
        .addFunction("GetSpeed", &SplinePath::GetSpeed)
        .addFunction("GetLength", &SplinePath::GetLength)
        .addFunction("GetPosition", &SplinePath::GetPosition)
        .addFunction("GetControlledNode", &SplinePath::GetControlledNode)
        .addFunction("GetPoint", &SplinePath::GetPoint)
        .addFunction("Move", &SplinePath::Move)
        .addFunction("Reset", &SplinePath::Reset)
        .addFunction("IsFinished", &SplinePath::IsFinished)

        .addProperty("interpolationMode", &SplinePath::GetInterpolationMode, &SplinePath::SetInterpolationMode)
        .addProperty("speed", &SplinePath::GetSpeed, &SplinePath::SetSpeed)
        .addProperty("length", &SplinePath::GetLength)
        .addProperty("position", &SplinePath::GetPosition, &SplinePath::SetPosition)
        .addProperty("controlledNode", &SplinePath::GetControlledNode, &SplinePath::SetControlledNode)
        .addProperty("finished", &SplinePath::IsFinished)
    );
}

static void RegisterUnknownComponent(kaguya::State& lua)
{
    using namespace kaguya;

    lua["KUnknownComponent"].setClass(UserdataMetatable<UnknownComponent, Component>()
        .addStaticFunction("new", &KCreateObject<UnknownComponent>)

        .addFunction("GetType", &UnknownComponent::GetType)
        .addFunction("GetTypeName", &UnknownComponent::GetTypeName)
        .addFunction("GetAttributes", &UnknownComponent::GetAttributes)

        .addProperty("type", &UnknownComponent::GetType, &UnknownComponent::SetType)
        .addProperty("typeName", &UnknownComponent::GetTypeName, &UnknownComponent::SetTypeName)
        .addProperty("attributes", &UnknownComponent::GetAttributes)
    );
}

static void RegisterValueAnimation(kaguya::State& lua)
{
    using namespace kaguya;

    // enum InterpMethod;
    lua["KIM_NONE"] = IM_NONE;
    lua["KIM_LINEAR"] = IM_LINEAR;
    lua["KIM_SPLINE"] = IM_SPLINE;

    lua["KValueAnimation"].setClass(UserdataMetatable<ValueAnimation, Resource>()
        .addStaticFunction("new", &KCreateObject<ValueAnimation>)

        .addFunction("SetInterpolationMethod", &ValueAnimation::SetInterpolationMethod)
        .addFunction("SetSplineTension", &ValueAnimation::SetSplineTension)
        .addFunction("SetValueType", &ValueAnimation::SetValueType)
        .addFunction("SetKeyFrame", &ValueAnimation::SetKeyFrame)
        .addFunction("SetEventFrame", &ValueAnimation::SetEventFrame)
        .addFunction("IsValid", &ValueAnimation::IsValid)
        .addFunction("GetOwner", &ValueAnimation::GetOwner)
        .addFunction("GetInterpolationMethod", &ValueAnimation::GetInterpolationMethod)
        .addFunction("GetSplineTension", &ValueAnimation::GetSplineTension)
        .addFunction("GetValueType", &ValueAnimation::GetValueType)
        .addFunction("GetBeginTime", &ValueAnimation::GetBeginTime)
        .addFunction("GetEndTime", &ValueAnimation::GetEndTime)
        .addFunction("GetAnimationValue", &ValueAnimation::GetAnimationValue)
        .addFunction("HasEventFrames", &ValueAnimation::HasEventFrames)
        .addFunction("GetEventFrames", &ValueAnimation::GetEventFrames)

        .addProperty("valid", &ValueAnimation::IsValid)
        .addProperty("owner", &ValueAnimation::GetOwner, &ValueAnimation::SetOwner)
        .addProperty("interpolationMethod", &ValueAnimation::GetInterpolationMethod, &ValueAnimation::SetInterpolationMethod)
        .addProperty("splineTension", &ValueAnimation::GetSplineTension, &ValueAnimation::SetSplineTension)
        .addProperty("valueType", &ValueAnimation::GetValueType, &ValueAnimation::SetValueType)
    );
}

void RegisterSceneLuaAPI(kaguya::State& lua)
{
    RegisterSerializable(lua);

    RegisterAnimatable(lua);
    RegisterAnimationDefs(lua);
    RegisterComponent(lua);
    RegisterLogicComponent(lua);
    RegisterNode(lua);
    RegisterObjectAnimation(lua);
    RegisterScene(lua);
    RegisterSceneEvents(lua);
    RegisterSmoothedTransform(lua);
    RegisterSplinePath(lua);
    RegisterUnknownComponent(lua);
    RegisterValueAnimation(lua);
}
}
