#include <algorithm>
#include <any>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Agent/HIKARI_AgentBridge.h"
#include "Editor/Agent/HIKARI_AgentConnection.h"
#include "Editor/Agent/HIKARI_AgentEngineRoots.h"
#include "Editor/Agent/HIKARI_AgentHost.h"
#include "Editor/Agent/HIKARI_AgentProjectFiles.h"
#include "Editor/Agent/HIKARI_HeadlessEngineRun.h"
#include "Editor/Agent/HIKARI_AgentSession.h"
#include "Editor/Authoring/HIKARI_ComponentPropertyAuthoringService.h"
#include "Editor/Authoring/HIKARI_EditorAuthoringSessionHost.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Commands/HIKARI_EditorOperationSearch.h"
#include "Editor/Projects/HIKARI_EditorChildProcess.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_ColliderComponent.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Components/HIKARI_SequencePlayerComponent.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Tools/Agent/HIKARI_AgentClient.h"

// 外部智能体（Claude Code・Codex）が编辑器を使うための契約：本地桥と AgentHost、操作の検索、
// 組件の属性、後台文書、编辑器から起動する CLI、引擎目录の只读。付費の模型は呼ばない。
using namespace HIKARI::EDITOR;
using Json = nlohmann::json;
#define CHECK(value)                                                                                                   \
    do {                                                                                                               \
        if (!(value))                                                                                                  \
            throw std::runtime_error(#value);                                                                          \
    } while (false)

namespace {

class Document final : public EditorAuthoringSession {
public:
    EditorOperationRegistry operations;
    std::string guid;
    int detached = 0;
    EditorOperationRegistry& Operations() override { return operations; }
    std::any ControllerAddress() override { return this; }
    EditorOperationResult Open(const Json& args) override {
        guid = args.value("assetGuid", std::string{});
        return {true, "OK", "Opened"};
    }
    Json Describe() const override { return {{"assetGuid", guid}, {"dirty", true}}; }
    EditorOperationResult Reveal() override { return {true, "OK", "Visible"}; }
    void DetachVisible() override { ++detached; }
};
void TestDocuments() {
    EditorAuthoringSessionHost host;
    EditorOperationRegistry registry;
    int created = 0;
    host.RegisterWorkspace({"test", "Test", [&] {
                                ++created;
                                return std::make_unique<Document>();
                            }});
    host.RegisterOperations(registry);
    auto first = registry.Execute("authoring.session.open", {{"workspace", "test"}, {"assetGuid", "asset-a"}});
    const std::string id = first.data.at("sessionId");
    CHECK(!host.ActivateDocument("test", {}, true)); // Preserve the human document.
    CHECK(host.ActivateDocument("test", {}, false));
    Document fallback;
    auto& live = host.ResolveController("test", fallback);
    CHECK(&live != &fallback);
    auto same = registry.Execute("authoring.session.open", {{"workspace", "test"}, {"assetGuid", "asset-a"}});
    CHECK(created == 1 && same.data.at("sessionId") == id);
    host.ShowForeground("test");
    CHECK(&host.ResolveController("test", fallback) == &fallback);
    CHECK(!host.ActivateDocument("test", {}, false)); // Explicit human selection persists.
    CHECK(host.ActivateDocument("test", "asset-a", true));
    CHECK(&host.ResolveController("test", fallback) == &live);
    CHECK(!registry.Execute("authoring.close", {{"sessionId", id}}).ok);
    for (int i = 0; i < 10; ++i)
        CHECK(registry.Execute("authoring.session.open", {{"workspace", "test"}}).ok);
    CHECK(host.WorkspaceDocuments("test").size() == 11);
}
void TestComponentSchemas() {
    HIKARI::ComponentRegistry registry;
    HIKARI::ComponentTypeInfo camera;
    camera.typeName = "CameraComponent";
    camera.factory = [] { return std::make_unique<HIKARI::CameraComponent>(); };
    CHECK(registry.Register(camera));
    HIKARI::ComponentTypeInfo player;
    player.typeName = "SequencePlayerComponent";
    player.factory = [] { return std::make_unique<HIKARI::SequencePlayerComponent>(); };
    CHECK(registry.Register(player));
    HIKARI::SceneDocument document;
    HIKARI::SceneObjectData object;
    object.id = HIKARI::SceneObjectId{1};
    object.components = {{"CameraComponent", Json::object(), HIKARI::SceneComponentId{1}},
                         {"SequencePlayerComponent", Json::object(), HIKARI::SceneComponentId{2}}};
    document.objects.push_back(object);
    ComponentPropertyAuthoringService service;
    const ComponentAuthoringTarget target{HIKARI::SceneObjectId{1}, HIKARI::SceneComponentId{2}};
    const auto schema = service.Describe(registry, document, target, {});
    CHECK(schema.ok);
    std::set<std::string> fields;
    for (const auto& field : schema.data["fields"])
        fields.insert(field["id"]);
    CHECK(fields.contains("sequenceAssetGuid") && fields.contains("playOnStart") && fields.contains("channelPolicy"));
    ComponentPropertyMutation mutation{target, "playOnStart", true};
    const auto changed = service.Apply(registry, document, std::span(&mutation, 1), {});
    CHECK(changed.ok && changed.documentChanged);
    CHECK(document.objects[0].components[1].properties["playOnStart"] == true);
    const auto unchanged = service.Apply(registry, document, std::span(&mutation, 1), {});
    CHECK(unchanged.ok && !unchanged.documentChanged);
    const auto before = document.objects[0].components;
    const ComponentAuthoringTarget lensTarget{HIKARI::SceneObjectId{1}, HIKARI::SceneComponentId{1}};
    const std::array invalidBatch{ComponentPropertyMutation{target, "playOnStart", false},
                                  ComponentPropertyMutation{lensTarget, "nearClip", 1.0e300}};
    const auto rejected = service.Apply(registry, document, invalidBatch, {});
    CHECK(!rejected.ok && rejected.code == "VALUE_OUT_OF_RANGE");
    CHECK(document.objects[0].components[1].properties == before[1].properties);
    CHECK(document.objects[0].components[0].properties == before[0].properties);

    const auto lens = service.Describe(registry, document, {HIKARI::SceneObjectId{1}, HIKARI::SceneComponentId{1}}, {});
    CHECK(lens.ok && lens.data["fields"].size() >= 4);

    // 形の寸法・列挙・三成分の値も安定 ID で読み書きできる（表示名だけの項目は無い）。
    HIKARI::ComponentTypeInfo mesh;
    mesh.typeName = "ProceduralMeshComponent";
    mesh.factory = [] { return std::make_unique<HIKARI::ProceduralMeshComponent>(); };
    CHECK(registry.Register(mesh));
    HIKARI::ComponentTypeInfo collider;
    collider.typeName = "ColliderComponent";
    collider.factory = [] { return std::make_unique<HIKARI::ColliderComponent>(); };
    CHECK(registry.Register(collider));
    document.objects[0].components.push_back({"ProceduralMeshComponent", Json::object(), HIKARI::SceneComponentId{3}});
    document.objects[0].components.push_back({"ColliderComponent", Json::object(), HIKARI::SceneComponentId{4}});
    const ComponentAuthoringTarget meshTarget{HIKARI::SceneObjectId{1}, HIKARI::SceneComponentId{3}};
    const ComponentAuthoringTarget colliderTarget{HIKARI::SceneObjectId{1}, HIKARI::SceneComponentId{4}};
    const std::array shapeEdits{ComponentPropertyMutation{meshTarget, "width", 3.0},
                                ComponentPropertyMutation{meshTarget, "kind", "Ramp"},
                                ComponentPropertyMutation{colliderTarget, "fitMode", "Manual"},
                                ComponentPropertyMutation{colliderTarget, "center", Json::array({1.0, 2.0, 3.0})}};
    CHECK(service.Apply(registry, document, shapeEdits, {}).ok);
    CHECK(document.objects[0].components[2].properties["width"] == 3.0);
    CHECK(document.objects[0].components[2].properties["kind"] == "Ramp");
    CHECK(document.objects[0].components[3].properties["center"] == Json::array({1.0, 2.0, 3.0}));
    const auto ramp = service.Describe(registry, document, meshTarget, {});
    std::set<std::string> rampFields;
    for (const auto& field : ramp.data["fields"])
        rampFields.insert(field["id"]);
    CHECK(ramp.ok && rampFields.contains("height") && rampFields.contains("depth") && !rampFields.contains("cuts"));
    HIKARI::ComponentRegistry throwing;
    camera.factory = []() -> std::unique_ptr<HIKARI::IComponent> { throw std::runtime_error("factory failed"); };
    CHECK(throwing.Register(camera));
    CHECK(!service.Describe(throwing, document, lensTarget, {}).ok);
}

// 外部智能体の受け口（AgentHost の排队・承認・访问模式）。主线程の代わりにこのスレッドで PumpActions を回す。
template <class Predicate>
void PumpUntil(AGENT::AgentHost& host, Predicate done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (!done() && std::chrono::steady_clock::now() < deadline) {
        host.PumpActions();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(done());
}

void TestAgentHost() {
    using Decision = AGENT::AgentHost::ApprovalDecision;
    AGENT::AgentHost host;
    std::vector<std::pair<std::string, Json>> executed;
    host.SetExecutor([&](const std::string& operation, const Json& parameters) {
        executed.emplace_back(operation, parameters);
        return EditorOperationResult{true, "OK", operation};
    });
    host.SetPermissionResolver([](std::string_view operation) -> std::optional<AGENT::AgentOperationPolicy> {
        if (operation == "test.save")
            return AGENT::AgentOperationPolicy{EditorOperationPermission::ModifyProjectFiles};
        // 编辑器の再起動のように毎回聞く操作。
        if (operation == "test.reload")
            return AGENT::AgentOperationPolicy{EditorOperationPermission::ExecuteExternalProcess, true};
        if (operation == "test.query")
            return AGENT::AgentOperationPolicy{EditorOperationPermission::ReadEditorState, false, true};
        return AGENT::AgentOperationPolicy{EditorOperationPermission::ModifyEditorState};
    });
    // 本地桥の接続スレッドからの呼び出しを模す。結果はこのスレッドが PumpActions を回すまで返らない。
    const auto call = [&host](std::string body) {
        return std::async(std::launch::async, [&host, body] { return host.Execute(body); });
    };
    const auto ready = [](std::future<Json>& future) {
        return future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
    };
    CHECK(host.Execute(R"({"operation":"test.edit"})")["code"] == "BRIDGE_CLOSED");
    host.SetOpen(true);
    CHECK(host.Execute(R"({"action":"test.edit"})")["code"] == "INVALID_REQUEST");
    CHECK(host.Execute(R"({"operation":"test.edit","parameters":[1]})")["code"] == "INVALID_REQUEST");

    // 操作名と引数は分かれていて、引数名 action は操作名とぶつからない。
    auto edit = call(R"({"operation":"test.edit","parameters":{"action":"create"}})");
    PumpUntil(host, [&] { return ready(edit); });
    CHECK(edit.get()["ok"] == true && executed.size() == 1u && executed[0].first == "test.edit" &&
          executed[0].second["action"] == "create");

    // 承認：一度だけ許可、拒否、本轮允许（EndTurn まで聞かない）。
    const auto approveNext = [&](Decision decision) {
        auto save = call(R"({"operation":"test.save"})");
        PumpUntil(host, [&] { return host.HasPendingApprovals(); });
        const auto approvals = host.PendingApprovals();
        CHECK(approvals.size() == 1u && approvals[0].operation == "test.save" &&
              approvals[0].permission == "modifyProjectFiles");
        host.ResolveApproval(approvals[0].id, decision);
        PumpUntil(host, [&] { return ready(save); });
        return save.get();
    };
    CHECK(approveNext(Decision::AllowOnce)["ok"] == true && executed.size() == 2u);
    CHECK(approveNext(Decision::Deny)["code"] == "USER_REJECTED" && executed.size() == 2u);
    CHECK(approveNext(Decision::AllowTurn)["ok"] == true && executed.size() == 3u);
    auto granted = call(R"({"operation":"test.save"})");
    PumpUntil(host, [&] { return ready(granted); });
    CHECK(granted.get()["ok"] == true && executed.size() == 4u && !host.HasPendingApprovals());
    // 毎回聞く操作は本轮允许を選んでも次もまた聞かれる。
    for (int attempt = 0; attempt < 2; ++attempt) {
        auto reload = call(R"({"operation":"test.reload"})");
        PumpUntil(host, [&] { return host.HasPendingApprovals(); });
        const auto approvals = host.PendingApprovals();
        CHECK(approvals.size() == 1u && approvals[0].approveEachTime);
        host.ResolveApproval(approvals[0].id, Decision::AllowTurn);
        PumpUntil(host, [&] { return ready(reload); });
        CHECK(reload.get()["ok"] == true && executed.size() == 5u + static_cast<size_t>(attempt));
    }
    host.EndTurn();
    auto afterTurn = call(R"({"operation":"test.save"})");
    PumpUntil(host, [&] { return host.HasPendingApprovals(); });
    // 访问模式を切り替えると、待っている承認と要求は取り消される。
    host.SetAccessMode(AGENT::AgentAccessMode::DevelopProject);
    CHECK(afterTurn.get()["code"] == "ACTION_CANCELED");
    host.PumpActions();
    CHECK(!host.HasPendingApprovals() && executed.size() == 6u);

    // 項目源码の操作は「开发项目」だけ。
    auto code = call(R"({"operation":"project.build.status"})");
    PumpUntil(host, [&] { return ready(code); });
    CHECK(code.get()["ok"] == true && executed.size() == 7u);
    host.SetAccessMode(AGENT::AgentAccessMode::Content);
    CHECK(host.Execute(R"({"operation":"project.build.status"})")["code"] == "PROJECT_ACCESS_REQUIRED");

    // 人が場面を操作している間は修改を待たせ、読むだけの操作は通す。松手すると順番どおりに実行する。
    bool editing = true;
    host.SetEditingCheck([&editing] { return editing; });
    auto deferredEdit = call(R"({"operation":"test.edit","parameters":{"order":1}})");
    auto query = call(R"({"operation":"test.query"})");
    PumpUntil(host, [&] { return ready(query); });
    for (int frame = 0; frame < 5; ++frame) {
        host.PumpActions();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(!ready(deferredEdit) && executed.size() == 8u && executed.back().first == "test.query");
    editing = false;
    PumpUntil(host, [&] { return ready(deferredEdit); });
    CHECK(deferredEdit.get()["ok"] == true && executed.size() == 9u && executed.back().second["order"] == 1);
    host.SetEditingCheck({});

    // 自動（無需批准）：入れた時に待っている承認は通り、毎回聞く操作も聞かずに通る。切ると元どおり聞く。
    auto pendingSave = call(R"({"operation":"test.save"})");
    PumpUntil(host, [&] { return host.HasPendingApprovals(); });
    host.SetAutoApprove(true);
    PumpUntil(host, [&] { return ready(pendingSave); });
    CHECK(pendingSave.get()["ok"] == true && executed.size() == 10u && !host.HasPendingApprovals());
    auto autoReload = call(R"({"operation":"test.reload"})");
    PumpUntil(host, [&] { return ready(autoReload); });
    CHECK(autoReload.get()["ok"] == true && executed.size() == 11u && !host.HasPendingApprovals());
    host.SetAutoApprove(false);
    auto askedAgain = call(R"({"operation":"test.save"})");
    PumpUntil(host, [&] { return host.HasPendingApprovals(); });
    host.ResolveApproval(host.PendingApprovals().front().id, Decision::Deny);
    CHECK(askedAgain.get()["code"] == "USER_REJECTED" && executed.size() == 11u);

    // 閉じると待っている要求は取り消される。
    auto waiting = call(R"({"operation":"test.edit"})");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    host.SetOpen(false);
    const Json closed = waiting.get();
    CHECK(closed["code"] == "ACTION_CANCELED" || closed["code"] == "BRIDGE_CLOSED");
    host.PumpActions();
    CHECK(executed.size() == 11u);
}

// 操作の検索と説明：智能体に見せる操作だけ、目的の語で並べ、条件が無ければ工作区ごとの件数だけ。
void TestOperationSearch() {
    EditorOperationRegistry registry;
    const auto add = [&](std::string id, std::string summary, std::string workspace, EditorOperationExposureMask mask,
                         std::vector<std::string> terms = {}) {
        EditorOperationDefinition definition{std::move(id),
                                             std::move(summary),
                                             std::move(workspace),
                                             {{"color", EditorOperationParameterType::String, false, "Color name"}},
                                             [](const Json&) { return EditorOperationResult{true, "OK", "done"}; }};
        definition.exposureMask = mask;
        definition.searchTerms = std::move(terms);
        CHECK(registry.Register(std::move(definition), EditorOperationKind::Mutation, false).ok);
    };
    add("scene.paint", "Paint the selected object", "scene", EditorOperationExposureMask::Default, {"颜色"});
    add("scene.hidden", "Paint for humans only", "scene", EditorOperationExposureMask::Human);
    add("vfx.add_layer", "Add a VFX layer", "vfx", EditorOperationExposureMask::Agent);
    const Json index = SearchEditorOperations(registry, EditorOperationExposureMask::Agent, {});
    CHECK(index["workspaces"]["scene"] == 1 && index["workspaces"]["vfx"] == 1 && index["operations"].empty());
    EditorOperationSearchQuery query;
    query.keyword = "PAINT";
    const Json found = SearchEditorOperations(registry, EditorOperationExposureMask::Agent, query);
    CHECK(found["total"] == 1 && found["operations"][0]["id"] == "scene.paint");
    CHECK(!found["operations"][0].contains("searchTerms") && !found["operations"][0].contains("entryPoint"));
    // 既定は引数名だけ。形は includeParameters か operations.describe。
    CHECK(found["operations"][0]["optional"] == Json::array({"color"}) &&
          !found["operations"][0].contains("parameters"));
    query.includeParameters = true;
    CHECK(!SearchEditorOperations(registry, EditorOperationExposureMask::Agent, query)["operations"][0]["parameters"][0]
               .contains("required"));
    query.includeParameters = false;
    query.keyword = "颜色";
    CHECK(SearchEditorOperations(registry, EditorOperationExposureMask::Agent, query)["total"] == 1);
    query = {};
    query.workspace = "vfx";
    const Json vfx = SearchEditorOperations(registry, EditorOperationExposureMask::Agent, query);
    CHECK(vfx["total"] == 1 && !vfx["operations"][0].contains("parameters"));
}

// 本地桥の実際の往復：承認待ちの遅い要求が他の要求を止めない、偽の令牌は拒否、再起動後は客户端がつなぎ直す。
void TestLocalBridge(const std::filesystem::path& root) {
    const auto project = root / "agent-bridge-project";
    std::filesystem::remove_all(project);
    std::filesystem::create_directories(project);
    std::ofstream(project / "Project.hikari") << "{}";
    std::mutex gate;
    std::condition_variable gateChanged;
    bool release = false;
    std::atomic<int> slowStarted{0};
    const auto executor = [&](std::string_view body) -> Json {
        const Json request = Json::parse(body);
        const std::string operation = request.value("operation", std::string{});
        if (operation == "slow") {
            ++slowStarted;
            std::unique_lock lock(gate);
            gateChanged.wait(lock, [&] { return release; });
        }
        return {{"ok", true}, {"code", "OK"}, {"message", operation}, {"data", request.value("parameters", Json{})}};
    };
    std::string error;
    auto bridge = std::make_unique<AGENT::AgentBridge>();
    CHECK(bridge->Start(executor, error));
    const auto connectionPath = AGENT::FindAgentConnectionPath(project);
    CHECK(connectionPath == project / "Library" / "EditorAI" / "bridge.json");
    CHECK(bridge->PublishConnection(connectionPath, "test", error));
    const auto connection = AGENT::ReadAgentConnection(connectionPath, error);
    CHECK(connection && connection->port != 0u && connection->token.size() == 64u);

    HIKARI::AGENT_CLI::AgentClient client({project, std::chrono::seconds(10), std::chrono::seconds(3)});
    std::optional<Json> slow;
    std::thread slowCall([&] {
        std::string slowError;
        slow = HIKARI::AGENT_CLI::AgentClient({project}).Call("slow", Json::object(), slowError);
    });
    const auto slowDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (slowStarted.load() == 0 && std::chrono::steady_clock::now() < slowDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto quick = client.Call("quick", {{"action", "create"}}, error);
    CHECK(quick && (*quick)["message"] == "quick" && (*quick)["data"]["action"] == "create");
    {
        std::lock_guard lock(gate);
        release = true;
    }
    gateChanged.notify_all();
    slowCall.join();
    CHECK(slow && (*slow)["ok"] == true);

    AGENT::AgentConnection forged = *connection;
    forged.token.assign(64u, '0');
    CHECK(AGENT::WriteAgentConnection(connectionPath, forged, error));
    std::string forgedError;
    CHECK(!HIKARI::AGENT_CLI::AgentClient({project, std::chrono::seconds(5), std::chrono::seconds(1)})
               .Call("quick", Json::object(), forgedError));
    CHECK(AGENT::WriteAgentConnection(connectionPath, *connection, error));

    // 再起動すると端口と令牌が変わる。客户端は连接文件を読み直してつなぎ直す。
    bridge->Stop();
    CHECK(!std::filesystem::exists(connectionPath));
    bridge = std::make_unique<AGENT::AgentBridge>();
    CHECK(bridge->Start(executor, error) && bridge->PublishConnection(connectionPath, "test", error));
    const auto afterRestart = client.Call("quick", Json::object(), error);
    CHECK(afterRestart && (*afterRestart)["message"] == "quick");
    bridge->Stop();
}

// 编辑器の中の常駐会話：子進程の標準入出力で CLI の協議を往復する。CLI の代わりに PowerShell の偽物を使う。
// 道具の権限確認が编辑器に来ること、本轮允许で同じ道具は聞かれないこと、用量が報告どおりに読まれること。
void WriteScript(const std::filesystem::path& path, const std::string& body) {
    std::ofstream(path, std::ios::binary)
        << "$ErrorActionPreference = 'Stop'\n"
           "function Send($line) { [Console]::Out.WriteLine($line); [Console]::Out.Flush() }\n"
        << body;
}

AGENT::AgentSessionRequest FakeRequest(const std::filesystem::path& folder, AGENT::AgentKind kind,
                                       const std::filesystem::path& script) {
    AGENT::AgentSessionRequest request{};
    request.kind = kind;
    request.executable = script;
    request.projectRoot = folder;
    request.hikariExecutable = folder / "hikari.exe";
    request.runFolder = folder / (std::string(kind == AGENT::AgentKind::ClaudeCode ? "claude" : "codex") + "-run");
    return request;
}

template <class Predicate>
void PollUntil(AGENT::AgentSession& session, Predicate done);
bool HasEvent(const AGENT::AgentSession& session, AGENT::AgentEvent::Kind kind, std::string_view text);

std::string NativeUtf8(const std::filesystem::path& path) {
    std::string text = HIKARI::TEXT::PathToUtf8(path);
    std::replace(text.begin(), text.end(), '/', '\\');
    return text;
}

// 引擎目录の判定：大文字小文字、.. を含む相対パス、引擎目录を含む目录、名前が前方一致するだけの目录、
// 命令の書き方（\\ の重ね、Git Bash）、操作の出力先引数、项目と引擎目录が重なる時の起動拒否。
void TestEngineRoots(const std::filesystem::path& root) {
    namespace TEXT = HIKARI::TEXT;
    const auto engine = root / "agent-engine";
    const auto project = root / "agent-engine-project";
    std::filesystem::create_directories(engine / "HIKARI");
    std::filesystem::create_directories(project);
    const std::vector<std::filesystem::path> roots{engine};
    std::string upper = TEXT::PathToUtf8(engine / "HIKARI" / "New.cpp");
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; });
    CHECK(AGENT::TouchesEngineRoots(roots, TEXT::PathFromUtf8(upper)));
    CHECK(AGENT::TouchesEngineRoots(roots, "../agent-engine/HIKARI/x.cpp", project));
    CHECK(AGENT::TouchesEngineRoots(roots, root));
    CHECK(!AGENT::TouchesEngineRoots(roots, project / "Source" / "Game.cpp"));
    CHECK(!AGENT::TouchesEngineRoots(roots, root / "agent-engine2" / "x.cpp"));
    const std::string native = NativeUtf8(engine);
    std::string doubled;
    for (const char c : native)
        doubled += c == '\\' ? std::string("\\\\") : std::string(1, c);
    const std::string generic = TEXT::PathToUtf8(engine);
    const std::string bash = "/" + std::string(1, static_cast<char>(std::tolower(generic[0]))) + generic.substr(2);
    CHECK(AGENT::MentionsEngineRoots(roots, "del \"" + doubled + "\\\\HIKARI\\\\x.cpp\""));
    CHECK(AGENT::MentionsEngineRoots(roots, "rm -rf " + bash + "/HIKARI"));
    CHECK(AGENT::MentionsEngineRoots(roots, "cd " + native));
    CHECK(!AGENT::MentionsEngineRoots(roots, "dir " + NativeUtf8(project)));

    const EditorOperationDefinition capture{
        "test.capture",
        "Capture",
        "general",
        {{"path", EditorOperationParameterType::String, false, "Output", {{"format", kOutputPathFormat}}},
         {"source", EditorOperationParameterType::String, false, "Input"}},
        [](const Json&) { return EditorOperationResult{true, "OK", "captured"}; }};
    CHECK(AGENT::FindEngineOutputParameter(capture, {{"path", upper}}, roots) == upper);
    CHECK(!AGENT::FindEngineOutputParameter(
        capture, {{"path", TEXT::PathToUtf8(project / "shot.png")}, {"source", native}}, roots));

    AGENT::AgentSession session;
    std::string error;
    for (const std::filesystem::path& overlapping : {engine / "Game", root}) {
        AGENT::AgentSessionRequest request{};
        request.executable = project / "fake.ps1";
        request.hikariExecutable = project / "hikari.exe";
        request.projectRoot = overlapping;
        request.runFolder = project / "overlap-run";
        request.protectedRoots = roots;
        CHECK(!session.Start(request, error) && error.find("engine directory") != std::string::npos);
    }
    CHECK(!AGENT::FindEngineRoots().empty());
}

// 書き込みの確認。偽の CLI は引擎目录のファイル、项目の Source/、引擎目录を消す命令（Codex は引擎目录を含む書き込み権限も）
// を順に求め、返ってきた答えを一行の文にする。どれも人に聞かずに決まる。
void ProbeWrites(const std::filesystem::path& root, const std::filesystem::path& folder, AGENT::AgentKind kind,
                 const std::filesystem::path& script, bool allowProjectEdits, std::string_view expected) {
    const auto engine = root / "agent-engine";
    std::filesystem::create_directories(engine);
    AGENT::AgentSessionRequest request = FakeRequest(folder, kind, script);
    request.allowProjectEdits = allowProjectEdits;
    request.protectedRoots = {engine};
    const Json probe{{"engine", HIKARI::TEXT::PathToUtf8(engine / "HIKARI" / "Editor" / "Agent.cpp")},
                     {"project", HIKARI::TEXT::PathToUtf8(folder / "Source" / "Game.cpp")},
                     {"command", "Remove-Item -Recurse '" + NativeUtf8(engine) + "\\HIKARI'"},
                     {"glob", HIKARI::TEXT::PathToUtf8(root) + "/*"}};
    AGENT::AgentSession session;
    std::string error;
    CHECK(session.Start(request, error));
    CHECK(session.Send({probe.dump()}, error));
    PollUntil(session, [&] { return !session.Busy(); });
    CHECK(HasEvent(session, AGENT::AgentEvent::Kind::Text, expected) && session.PendingApprovals().empty());
    CHECK(HasEvent(session, AGENT::AgentEvent::Kind::Blocked, "Agent.cpp"));
    CHECK(HasEvent(session, AGENT::AgentEvent::Kind::Blocked, "Remove-Item"));
    CHECK(HasEvent(session, AGENT::AgentEvent::Kind::Blocked, "Game.cpp") == !allowProjectEdits);
    session.Stop();
}

template <class Predicate>
void PollUntil(AGENT::AgentSession& session, Predicate done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!done() && std::chrono::steady_clock::now() < deadline) {
        session.Poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(done());
}

bool HasEvent(const AGENT::AgentSession& session, AGENT::AgentEvent::Kind kind, std::string_view text) {
    return std::any_of(session.Events().begin(), session.Events().end(), [kind, text](const AGENT::AgentEvent& event) {
        return event.kind == kind && event.text.find(text) != std::string::npos;
    });
}

void TestClaudeSession(const std::filesystem::path& root) {
    const auto folder = root / "agent-session-claude";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const auto script = folder / "fake-claude.ps1";
    WriteScript(script, R"PS(
while ($true) {
  $line = [Console]::In.ReadLine()
  if ($null -eq $line) { break }
  $m = $line | ConvertFrom-Json
  if ($m.type -eq 'control_request' -and $m.request.subtype -eq 'initialize') {
    Send ('{"type":"control_response","response":{"subtype":"success","request_id":"' + $m.request_id + '","response":{}}}')
  } elseif ($m.type -eq 'control_request' -and $m.request.subtype -eq 'get_context_usage') {
    Send ('{"type":"control_response","response":{"subtype":"success","request_id":"' + $m.request_id + '","response":{"totalTokens":1200,"maxTokens":200000}}}')
  } elseif ($m.type -eq 'user' -and $m.message.content[0].text.StartsWith('{')) {
    $p = $m.message.content[0].text | ConvertFrom-Json
    $answers = @()
    $n = 0
    foreach ($q in @(@{tool='Write'; input=@{file_path=$p.engine; content='x'}}, @{tool='Edit'; input=@{file_path=$p.project; old_string='a'; new_string='b'}}, @{tool='Bash'; input=@{command=$p.command}})) {
      $n++
      Send (@{type='control_request'; request_id="w$n"; request=@{subtype='can_use_tool'; tool_name=$q.tool; input=$q.input}} | ConvertTo-Json -Compress -Depth 6)
      $answers += ([Console]::In.ReadLine() | ConvertFrom-Json).response.response.behavior
    }
    $mode = $args[[array]::IndexOf($args, '--permission-mode') + 1]
    $text = 'probe ' + ($answers -join ' ') + ' mode=' + $mode + ' editsOff=' + ($args -contains 'Edit Write MultiEdit NotebookEdit') + ' engineRule=' + (@($args | Where-Object { $_ -like 'Edit(//*/agent-engine/`*`*)' }).Count -eq 1) + ' strict=' + ($args -contains '--strict-mcp-config') + ' prompts=' + $args[[array]::IndexOf($args, '--permission-prompt-tool') + 1]
    Send (@{type='assistant'; message=@{content=@(@{type='text'; text=$text})}} | ConvertTo-Json -Compress -Depth 6)
    Send '{"type":"result","subtype":"success","num_turns":1,"result":"probe","usage":{"input_tokens":1,"output_tokens":1}}'
  } elseif ($m.type -eq 'user') {
    Send '{"type":"system","subtype":"init","session_id":"s1","mcp_servers":[{"name":"hikari","status":"connected"}]}'
    Send ('{"type":"assistant","message":{"content":[{"type":"text","text":"Echo: ' + $m.message.content[0].text + '"}]}}')
    Send '{"type":"control_request","request_id":"perm-1","request":{"subtype":"can_use_tool","tool_name":"Bash","input":{"command":"dir"},"description":"List files"}}'
    $answer = ([Console]::In.ReadLine() | ConvertFrom-Json)
    if ($answer.response.response.behavior -ne 'allow') { Send '{"type":"result","subtype":"error_during_execution","is_error":true}'; continue }
    Send '{"type":"control_request","request_id":"perm-2","request":{"subtype":"can_use_tool","tool_name":"Bash","input":{"command":"dir /b"}}}'
    $second = ([Console]::In.ReadLine() | ConvertFrom-Json)
    Send '{"type":"rate_limit_event","rate_limit_info":{"status":"allowed_warning","rateLimitType":"five_hour","utilization":0.34,"resetsAt":4102444800}}'
    Send ('{"type":"result","subtype":"success","num_turns":1,"result":"second ' + $second.response.response.behavior + '","usage":{"input_tokens":10,"output_tokens":5},"total_cost_usd":0.25}')
  }
}
)PS");
    AGENT::AgentSession session;
    std::string error;
    CHECK(session.Start(FakeRequest(folder, AGENT::AgentKind::ClaudeCode, script), error));
    CHECK(session.Send({"hello"}, error) && session.Busy());
    PollUntil(session, [&] { return !session.PendingApprovals().empty(); });
    const AGENT::AgentToolApproval approval = session.PendingApprovals().front();
    // 命令は説明ではなく全文を見せ、沙箱が無いことを知らせる。
    CHECK(approval.tool == "Bash" && approval.detail == "dir" && approval.warning == "command" &&
          session.SessionId() == "s1");
    session.ResolveApproval(approval.id, AGENT::AgentToolDecision::AllowTurn);
    PollUntil(session, [&] { return !session.Busy(); });
    CHECK(HasEvent(session, AGENT::AgentEvent::Kind::Text, "Echo: hello"));
    // 二つ目の Bash は本轮允许で聞かれずに通った。
    CHECK(HasEvent(session, AGENT::AgentEvent::Kind::Result, "second allow") && session.PendingApprovals().empty());
    PollUntil(session, [&] { return session.Usage().contextWindow == 200000u; });
    const AGENT::AgentUsage& usage = session.Usage();
    CHECK(usage.windows.size() == 1u && usage.windows[0].name == "five_hour" && usage.windows[0].usedPercent == 34 &&
          usage.windows[0].resetsAt == 4102444800);
    CHECK(usage.limitStatus == "allowed_warning" && usage.turnOutputTokens == 5u && usage.referenceCostUsd == 0.25 &&
          usage.contextTokens == 1200u);
    // 次の一轮では本轮允许は消えていて、また聞かれる。拒否すると CLI は失敗を返す。
    CHECK(session.Send({"again"}, error));
    PollUntil(session, [&] { return !session.PendingApprovals().empty(); });
    session.ResolveApproval(session.PendingApprovals().front().id, AGENT::AgentToolDecision::Deny);
    PollUntil(session, [&] { return !session.Busy(); });
    CHECK(session.Events().back().kind == AGENT::AgentEvent::Kind::Result && session.Events().back().failed);
    session.Stop();
    CHECK(!session.Running());
    // 自動（無需批准）：命令は人に聞かずに通り、「自動批准」として記録に残る。
    AGENT::AgentSessionRequest automatic = FakeRequest(folder, AGENT::AgentKind::ClaudeCode, script);
    automatic.autoApprove = true;
    automatic.allowProjectEdits = true;
    CHECK(session.Start(automatic, error) && session.Send({"hello"}, error));
    PollUntil(session, [&] { return !session.Busy(); });
    CHECK(HasEvent(session, AGENT::AgentEvent::Kind::Result, "second allow") && session.PendingApprovals().empty());
    CHECK(HasEvent(session, AGENT::AgentEvent::Kind::AutoApproved, "dir"));
    session.Stop();
    // 内容模式では自動でも命令は通さない（文件を変えない約束）。
    automatic.allowProjectEdits = false;
    CHECK(session.Start(automatic, error) && session.Send({"hello"}, error));
    PollUntil(session, [&] { return !session.Busy(); });
    CHECK(HasEvent(session, AGENT::AgentEvent::Kind::Blocked, "dir") && session.PendingApprovals().empty());
    session.Stop();
    // 利用者設定の defaultMode を使わず、文件编辑は编辑器が書き込み先で決める。引擎目录は CLI の deny 規則でも拒む。
    ProbeWrites(root, folder, AGENT::AgentKind::ClaudeCode, script, true,
                "probe deny allow deny mode=manual editsOff=False engineRule=True strict=True prompts=stdio");
    ProbeWrites(root, folder, AGENT::AgentKind::ClaudeCode, script, false,
                "probe deny deny deny mode=manual editsOff=True engineRule=True strict=True prompts=stdio");
}

void TestCodexSession(const std::filesystem::path& root) {
    const auto folder = root / "agent-session-codex";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const auto script = folder / "fake-codex.ps1";
    WriteScript(script, R"PS(
while ($true) {
  $line = [Console]::In.ReadLine()
  if ($null -eq $line) { break }
  $m = $line | ConvertFrom-Json
  switch ($m.method) {
    'initialize' { Send ('{"id":' + $m.id + ',"result":{"userAgent":"fake"}}') }
    'account/rateLimits/read' { Send ('{"id":' + $m.id + ',"result":{"rateLimits":{"primary":{"usedPercent":14,"windowDurationMins":10080,"resetsAt":4102444800},"secondary":null,"planType":"pro"}}}') }
    'model/list' { Send ('{"id":' + $m.id + ',"result":{"data":[{"id":"m1","model":"gpt-x","displayName":"GPT X","description":"","hidden":false,"isDefault":true,"defaultReasoningEffort":"medium","supportedReasoningEfforts":[{"reasoningEffort":"low","description":""},{"reasoningEffort":"medium","description":""}]},{"id":"m2","model":"gpt-hidden","displayName":"Hidden","description":"","hidden":true,"isDefault":false,"defaultReasoningEffort":"low","supportedReasoningEfforts":[]}]}}') }
    'thread/start' {
      $sandbox = $m.params.sandbox
      if ($m.params.approvalPolicy -ne 'on-request') { Send ('{"id":' + $m.id + ',"error":{"message":"bad approval policy"}}') }
      else { Send ('{"id":' + $m.id + ',"result":{"thread":{"id":"t1"}}}') }
    }
    'turn/start' {
      Send ('{"id":' + $m.id + ',"result":{"turn":{"id":"u1"}}}')
      if ($m.params.input[0].text.StartsWith('{')) {
        $p = $m.params.input[0].text | ConvertFrom-Json
        $answers = @()
        $n = 0
        foreach ($path in @($p.engine, $p.project)) {
          $n++
          Send (@{method='item/started'; params=@{threadId='t1'; turnId='u1'; item=@{type='fileChange'; id="f$n"; status='inProgress'; changes=@(@{path=$path; kind=@{type='add'}; diff=''})}}} | ConvertTo-Json -Compress -Depth 8)
          Send (@{id="file-$n"; method='item/fileChange/requestApproval'; params=@{threadId='t1'; turnId='u1'; itemId="f$n"; startedAtMs=0}} | ConvertTo-Json -Compress -Depth 8)
          $answers += ([Console]::In.ReadLine() | ConvertFrom-Json).result.decision
        }
        Send (@{id='cmd'; method='item/commandExecution/requestApproval'; params=@{threadId='t1'; turnId='u1'; itemId='c1'; startedAtMs=0; command=$p.command}} | ConvertTo-Json -Compress -Depth 8)
        $answers += ([Console]::In.ReadLine() | ConvertFrom-Json).result.decision
        Send (@{id='perm'; method='item/permissions/requestApproval'; params=@{threadId='t1'; turnId='u1'; itemId='p1'; startedAtMs=0; permissions=@{fileSystem=@{entries=@(@{access='write'; path=@{type='glob_pattern'; pattern=$p.glob}})}}}} | ConvertTo-Json -Compress -Depth 10)
        $granted = ([Console]::In.ReadLine() | ConvertFrom-Json).result.permissions.fileSystem
        $answers += $(if ($null -eq $granted) { 'denied' } else { 'granted' })
        $policy = $m.params.sandboxPolicy
        $roots = $(if ($null -eq $policy.writableRoots) { '-' } else { @($policy.writableRoots).Count })
        $text = 'probe ' + ($answers -join ' ') + ' sandbox=' + $sandbox + ' policy=' + $policy.type + ' roots=' + $roots + ' mcpApproved=' + ($args -join ' ').Contains('hikari.default_tools_approval_mode')
        Send (@{method='item/completed'; params=@{threadId='t1'; turnId='u1'; completedAtMs=0; item=@{type='agentMessage'; id='a3'; text=$text}}} | ConvertTo-Json -Compress -Depth 8)
        Send '{"method":"turn/completed","params":{"threadId":"t1","turn":{"id":"u1","items":[],"status":"completed"}}}'
        continue
      }
      Send '{"id":"srv-1","method":"item/commandExecution/requestApproval","params":{"threadId":"t1","turnId":"u1","itemId":"i1","startedAtMs":0,"command":"dir"}}'
      $answer = ([Console]::In.ReadLine() | ConvertFrom-Json)
      Send ('{"method":"item/completed","params":{"threadId":"t1","turnId":"u1","completedAtMs":0,"item":{"type":"agentMessage","id":"a1","text":"decision ' + $answer.result.decision + ' for ' + $m.params.input[0].text + '"}}}')
      Send '{"id":"srv-2","method":"item/tool/requestUserInput","params":{}}'
      $refused = ([Console]::In.ReadLine() | ConvertFrom-Json)
      Send ('{"method":"item/completed","params":{"threadId":"t1","turnId":"u1","completedAtMs":0,"item":{"type":"agentMessage","id":"a2","text":"input refused ' + $refused.error.code + '"}}}')
      Send '{"method":"thread/tokenUsage/updated","params":{"threadId":"t1","turnId":"u1","tokenUsage":{"total":{"totalTokens":900,"inputTokens":800,"outputTokens":100,"cachedInputTokens":0,"reasoningOutputTokens":0},"last":{"totalTokens":300,"inputTokens":250,"outputTokens":50,"cachedInputTokens":0,"reasoningOutputTokens":0},"modelContextWindow":272000}}}'
      Send '{"method":"turn/completed","params":{"threadId":"t1","turn":{"id":"u1","items":[],"status":"completed"}}}'
    }
  }
}
)PS");
    AGENT::AgentSession session;
    std::string error;
    CHECK(session.Start(FakeRequest(folder, AGENT::AgentKind::Codex, script), error));
    // thread/start の返事より先に送っても、thread ができてから turn/start が出る。
    CHECK(session.Send({"build it"}, error));
    PollUntil(session, [&] { return !session.PendingApprovals().empty(); });
    const AGENT::AgentToolApproval approval = session.PendingApprovals().front();
    CHECK(approval.tool == "command" && approval.detail == "dir" && approval.warning == "command" &&
          session.SessionId() == "t1");
    session.ResolveApproval(approval.id, AGENT::AgentToolDecision::AllowOnce);
    PollUntil(session, [&] { return !session.Busy(); });
    CHECK(HasEvent(session, AGENT::AgentEvent::Kind::Text, "decision accept for build it"));
    CHECK(HasEvent(session, AGENT::AgentEvent::Kind::Text, "input refused -32601"));
    const AGENT::AgentUsage& usage = session.Usage();
    CHECK(usage.windows.size() == 1u && usage.windows[0].name == "primary" && usage.windows[0].usedPercent == 14 &&
          usage.windows[0].windowMinutes == 10080 && usage.plan == "pro");
    CHECK(usage.contextTokens == 300u && usage.contextWindow == 272000u && usage.turnOutputTokens == 50u);
    // model/list の報告：隠れた模型は除き、選べる推理强度と既定を持つ。
    const std::vector<AGENT::AgentModelOption>& models = session.Models();
    CHECK(models.size() == 1u && models[0].id == "gpt-x" && models[0].label == "GPT X" && models[0].isDefault &&
          models[0].efforts == std::vector<std::string>({"low", "medium"}) && models[0].defaultEffort == "medium");
    CHECK(!AGENT::DefaultAgentModels(AGENT::AgentKind::ClaudeCode).empty() &&
          AGENT::DefaultAgentModels(AGENT::AgentKind::Codex).empty());
    session.Stop();
    // 沙箱は毎轮明示し、利用者設定の writable_roots を引き継がない。
    ProbeWrites(
        root, folder, AGENT::AgentKind::Codex, script, true,
        "probe decline accept decline denied sandbox=workspace-write policy=workspaceWrite roots=0 mcpApproved=True");
    ProbeWrites(root, folder, AGENT::AgentKind::Codex, script, false,
                "probe decline decline decline denied sandbox=read-only policy=readOnly roots=- mcpApproved=True");
}

// 後ろで動かす無頭の引擎の引数と结果の拾い方：本地桥を開かない、輸入轨道があれば Play、模块が無ければ模块なし、
// 連続截图の名前、その回に増えた日志だけを読む。
void TestHeadlessEngineRun(const std::filesystem::path& root) {
    const auto folder = root / "agent-headless";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder / "Logs");
    // 後ろの编辑器は request.json の play.run を走らせ、同じ目录の result.json に結果を書く。
    AGENT::HeadlessEngineRun run;
    run.descriptor = folder / "Project.hikari";
    run.request = folder / "request.json";
    run.ticks = 300u;
    const std::vector<std::wstring> arguments = AGENT::BuildHeadlessEngineArguments(run);
    const auto has = [&arguments](std::wstring_view value) {
        return std::find(arguments.begin(), arguments.end(), value) != arguments.end();
    };
    CHECK(has(L"--agent-bridge-off") && has(L"--agent-play-run") && has(L"--allow-missing-project-module") &&
          !has(L"--play-on-start"));
    CHECK(AGENT::ReadHeadlessPlayResult(run).is_null());
    std::ofstream(folder / "result.json") << "{\"ok\": true, \"data\": {\"captures\": []}}";
    CHECK(AGENT::ReadHeadlessPlayResult(run).value("ok", false));
    std::ofstream(folder / "Logs" / "editor.log") << "[WARN] old\n";
    const auto before = AGENT::ListLogFiles(folder / "Logs");
    std::ofstream(folder / "Logs" / "run.log") << "[INFO] ok\n[WARN] slow\n[ERROR] missing\n";
    // 人の编辑器の日志が後から書かれても、その回に増えた日志を選ぶ。
    std::ofstream(folder / "Logs" / "editor.log", std::ios::app) << "[ERROR] editor\n";
    const auto log = AGENT::FindNewLogFile(folder / "Logs", before);
    CHECK(log.filename() == "run.log" && AGENT::ReadLogProblems(log).size() == 2u);
    // 後ろの引擎は固定の大きさで描く。light は画質を落とし、project は项目の画質（renderQuality を渡さない）。
    const Json light = AGENT::HeadlessRuntimeSettings(true);
    const Json project = AGENT::HeadlessRuntimeSettings(false);
    CHECK(light["windowWidth"] == 1280 && light["renderQuality"]["dynamicGiQuality"] == "Low");
    CHECK(project["windowWidth"] == 1920 && !project.contains("renderQuality"));
}

// 智能体の説明文件：初回は三つ書き、内容が同じなら書かない。利用者が書いた项目约定の節は書き直しても残る。
void TestAgentProjectFiles(const std::filesystem::path& root) {
    const auto folder = root / "agent-files";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    AGENT::AgentProjectFileInputs inputs{"Probe", folder / "hikari.exe", {}};
    std::vector<std::filesystem::path> written;
    std::string error;
    CHECK(AGENT::WriteAgentProjectFiles(folder, inputs, written, error) && written.size() == 3u);
    CHECK(AGENT::WriteAgentProjectFiles(folder, inputs, written, error) && written.empty());
    std::ofstream(folder / "AGENTS.md", std::ios::app) << "\n- 画风：低多边形\n";
    inputs.projectName = "Renamed";
    CHECK(AGENT::WriteAgentProjectFiles(folder, inputs, written, error) && written.size() == 1u);
    std::ifstream input(folder / "AGENTS.md", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    CHECK(text.find("Renamed") != std::string::npos && text.find("低多边形") != std::string::npos);
}

// 登录の確認：Claude は auth status の loggedIn を読み、Codex は login status の「Not logged in」で見分ける。
// それ以外の失敗（設定ファイルが読めないなど）は未登录と言わず、最初の行を見せる。模型は呼ばない。
void TestLoginCheck(const std::filesystem::path& root) {
    const auto folder = root / "agent-login";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const auto loggedIn = folder / "logged-in.ps1";
    const auto claudeLoggedOut = folder / "claude-logged-out.ps1";
    const auto codexLoggedOut = folder / "codex-logged-out.ps1";
    const auto broken = folder / "broken.ps1";
    WriteScript(loggedIn, "Send '{\"loggedIn\": true}'\n");
    WriteScript(claudeLoggedOut, "Send '{\"loggedIn\": false}'\nexit 1\n");
    WriteScript(codexLoggedOut, "[Console]::Error.WriteLine('Not logged in')\nexit 1\n");
    WriteScript(broken, "[Console]::Error.WriteLine('Error loading config.toml: unknown variant ultra')\nexit 1\n");
    using State = AGENT::AgentLoginCheck::State;
    struct Outcome {
        State state;
        std::string command;
        std::string detail;
    };
    const auto check = [&folder](AGENT::AgentKind kind, const std::filesystem::path& script) {
        AGENT::AgentLoginCheck login;
        login.Start(kind, script, folder / "status.txt");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (login.GetState() == State::Checking && std::chrono::steady_clock::now() < deadline) {
            login.Poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return Outcome{login.GetState(), login.LoginCommand(), login.Detail()};
    };
    CHECK(check(AGENT::AgentKind::ClaudeCode, loggedIn).state == State::LoggedIn);
    const Outcome claudeOut = check(AGENT::AgentKind::ClaudeCode, claudeLoggedOut);
    CHECK(claudeOut.state == State::LoggedOut && claudeOut.command.ends_with("auth login"));
    CHECK(check(AGENT::AgentKind::Codex, loggedIn).state == State::LoggedIn);
    const Outcome codexOut = check(AGENT::AgentKind::Codex, codexLoggedOut);
    CHECK(codexOut.state == State::LoggedOut && codexOut.command.ends_with("\" login"));
    const Outcome failed = check(AGENT::AgentKind::Codex, broken);
    CHECK(failed.state == State::Failed && failed.detail.find("unknown variant ultra") != std::string::npos);
    CHECK(check(AGENT::AgentKind::Codex, {}).state == State::Unknown);
}

} // namespace

int main(int argc, char** argv) {
    try {
        CHECK(argc == 2);
        const std::filesystem::path root(reinterpret_cast<const char8_t*>(argv[1]));
        TestDocuments();
        TestComponentSchemas();
        TestAgentHost();
        TestOperationSearch();
        TestEngineRoots(root);
        TestLocalBridge(root);
        TestClaudeSession(root);
        TestCodexSession(root);
        TestLoginCheck(root);
        TestHeadlessEngineRun(root);
        TestAgentProjectFiles(root);
        std::cout << "PASS agent host, bridge, operation search, component properties, agent session and engine "
                     "directory contracts\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
