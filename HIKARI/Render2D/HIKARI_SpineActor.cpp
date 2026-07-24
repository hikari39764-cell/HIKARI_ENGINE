#include "Render2D/HIKARI_SpineActor.h"
#include "Render2D/HIKARI_SpineTextureLoader.h"
#include "Matrix3x3.h"
#include <spine/RegionAttachment.h>
#include <spine/TextureRegion.h>
#include <spine/Animation.h>
namespace HIKARI {

    SpineActor::~SpineActor() {
        delete state_;
        delete stateData_;
        delete skeleton_;
        delete atlas_;
        if (listener_) {
            delete listener_;
            listener_ = nullptr;
        }
    }

    bool SpineActor::Load(const std::string& atlasPath, const std::string& jsonPath) {
        static SpineDxTextureLoader loader;

        atlas_ = new spine::Atlas(atlasPath.c_str(), &loader);
        if (!atlas_) return false;

        spine::SkeletonJson json(atlas_);
        skelData_ = json.readSkeletonDataFile(jsonPath.c_str());
        if (!skelData_) return false;

        skeleton_ = new spine::Skeleton(skelData_);
        stateData_ = new spine::AnimationStateData(skelData_);
        state_ = new spine::AnimationState(stateData_);
        skeleton_->setScaleY(-1.0f);
        return true;
    }

    void SpineActor::SetAnimation(const char* name, bool loop, int trackIndex, float mixDuration, bool forceSet) {
        if (!state_ || !name) return;

        spine::TrackEntry* current = state_->getCurrent(trackIndex);

        if (!forceSet && current) {
            spine::Animation* anim = current->getAnimation();
            if (anim && strcmp(anim->getName().buffer(), name) == 0) {
                return;
            }
        }

        spine::TrackEntry* e = state_->setAnimation(trackIndex, name, loop);
        if (mixDuration >= 0.0f && e) {
            e->setMixDuration(mixDuration);
        }
    }


    void SpineActor::SetSkin(const char* skinName) {
        if (!skeleton_) return;
        skeleton_->setSkin(skinName);
        skeleton_->setSlotsToSetupPose();
    }

    void SpineActor::Update(float dt, bool usePhysics) {
        if (!skeleton_ || !state_) return;

        skeleton_->update(dt);
        state_->update(dt);
        state_->apply(*skeleton_);

        const auto phys = usePhysics ? spine::Physics::Physics_Update
            : spine::Physics::Physics_None;

        const auto oneArg =
            static_cast<void (spine::Skeleton::*)(spine::Physics)>
            (&spine::Skeleton::updateWorldTransform);
        (skeleton_->*oneArg)(phys);
    }

    void SpineActor::Draw() {
        if (!skeleton_ || !state_) return;

        // 1) まず骨格をワールド座標に更新（物理を無効）
        skeleton_->updateWorldTransform(spine::Physics::Physics_None);

        // 2) 2D 変換
        const Matrix3x3 mWorld = transform.ToWorld(0.0f, 0.0f);
        auto TM = [&](float x, float y) -> Vector2 {
            return Vector2{
                x * mWorld.m[0][0] + y * mWorld.m[1][0] + mWorld.m[2][0],
                x * mWorld.m[0][1] + y * mWorld.m[1][1] + mWorld.m[2][1]
            };
            };

        // 3) 当フレームのレンダリングコマンドのリストを生成
        spine::RenderCommand* cmd = renderer_.render(*skeleton_);
        for (; cmd; cmd = cmd->next) {
            // テクスチャハンドルを取得
            int texHandle = -1;
            if (cmd->texture) {
                SpineTexture* tex = static_cast<SpineTexture*>(cmd->texture);
                texHandle = tex->handle;
            }
            if (texHandle < 0) continue;

            // 頂点とインデックス
            const float* pos = cmd->positions;
            const float* uv = cmd->uvs;
            const unsigned short* idx = cmd->indices;

            const int numV = cmd->numVertices;
            const int numI = cmd->numIndices;

            auto X = [&](int i) { return pos[i * 2 + 0]; };
            auto Y = [&](int i) { return pos[i * 2 + 1]; };
            auto U = [&](int i) { return uv[i * 2 + 0]; };
            auto V = [&](int i) { return uv[i * 2 + 1]; };


            if (idx && numI >= 3) {
                for (int k = 0; k + 2 < numI; k += 3) {
                    int i0 = idx[k + 0], i1 = idx[k + 1], i2 = idx[k + 2];
                    if (i0 < 0 || i1 < 0 || i2 < 0) continue;
                    if (i0 >= numV || i1 >= numV || i2 >= numV) continue;

                    Vector2 p0 = TM(X(i0), Y(i0));
                    Vector2 p1 = TM(X(i1), Y(i1));
                    Vector2 p2 = TM(X(i2), Y(i2));

                    RENDERER::DrawMeshQuadHandleUV_Vertices(
                        texHandle,
                        /* lt */ p0, /* rt */ p1, /* lb */ p2, /* rb=lb */ p2,
                        /* uv lt */ U(i0), V(i0),
                        /* uv rt */ U(i1), V(i1),
                        /* uv lb */ U(i2), V(i2),
                        /* uv rb */ U(i2), V(i2),
                        enableCamera ? RENDERER::CameraMode::Inherit : RENDERER::CameraMode::Ignore,
                        0xFFFFFFFF
                    );
                }
                continue;
            }

            if (numV == 4 && numI == 0) {
                Vector2 p0 = TM(X(0), Y(0));
                Vector2 p1 = TM(X(1), Y(1));
                Vector2 p2 = TM(X(2), Y(2));
                Vector2 p3 = TM(X(3), Y(3));

                RENDERER::DrawMeshQuadHandleUV_Vertices(
                    texHandle,
                    p0, p1, p2, p3,
                    U(0), V(0),
                    U(1), V(1),
                    U(2), V(2),
                    U(3), V(3),
                    enableCamera ? RENDERER::CameraMode::Inherit : RENDERER::CameraMode::Ignore,
                    0xFFFFFFFF
                );
                continue;
            }

            if (numI == 0 && numV >= 3) {
                for (int i = 0; i + 2 < numV; i += 3) {
                    Vector2 p0 = TM(X(i + 0), Y(i + 0));
                    Vector2 p1 = TM(X(i + 1), Y(i + 1));
                    Vector2 p2 = TM(X(i + 2), Y(i + 2));

                    RENDERER::DrawMeshQuadHandleUV_Vertices(
                        texHandle,
                        p0, p1, p2, p2,
                        U(i + 0), V(i + 0),
                        U(i + 1), V(i + 1),
                        U(i + 2), V(i + 2),
                        U(i + 2), V(i + 2),
                        enableCamera ? RENDERER::CameraMode::Inherit : RENDERER::CameraMode::Ignore,
                        0xFFFFFFFF
                    );
                }
                continue;
            }

            // その他のケースは一旦スキップ
        }
    }
    void SpineActor::AddAnimation(const char* name, bool loop, float delaySec, bool forceAdd) {
        if (!state_ || !name) return;

        if (!forceAdd) {
            spine::TrackEntry* entry = state_->getCurrent(0);
            while (entry) {
                spine::Animation* anim = entry->getAnimation();
                if (anim && strcmp(anim->getName().buffer(), name) == 0) {
                    return;
                }
                entry = entry->getNext();
            }
        }
        state_->addAnimation(0, name, loop, delaySec);
    }

    void SpineActor::SetMix(const char* from, const char* to, float mixSec) {
        if (!stateData_ || !from || !to) return;
        stateData_->setMix(spine::String(from), spine::String(to), mixSec);
    }
    void SpineActor::SetEmptyAnimation(int track, float mixSec, bool forceSet) {
        if (!state_) return;

        if (!forceSet) {
            spine::TrackEntry* current = state_->getCurrent(track);
            if (current) {
                spine::Animation* anim = current->getAnimation();
                if (anim && strcmp(anim->getName().buffer(), "empty") == 0) {
                    return;
                }
            }
        }

        state_->setEmptyAnimation(track, mixSec);
    }
    void SpineActor::ClearTrack(int track) {
        if (!state_) return;
        state_->clearTrack(track);
    }
    void SpineActor::ClearTracks() {
        if (!state_) return;
        state_->clearTracks();
    }

    void SpineActor::SetStateListener(StateListener cb) {
        if (!state_) return;

        // リスナーを解除
        if (!cb) {
            state_->setListener((spine::AnimationStateListenerObject*)nullptr);
            if (listener_) { delete listener_; listener_ = nullptr; }
            return;
        }

        // リスナーを差し替え
        if (listener_) { delete listener_; listener_ = nullptr; }
        listener_ = new ListenerProxy(std::move(cb));
        state_->setListener(listener_);
    }

    bool SpineActor::IsTrackFinished(int trackIndex) {
        if (!state_) return true;

        spine::TrackEntry* entry = state_->getCurrent(trackIndex);
        if (!entry) {
            return true;
        }

        return entry->isComplete();
    }

    bool SpineActor::IsAnimationFinished(const char* name, int trackIndex) {
        if (!state_ || !name) return false;

        spine::TrackEntry* entry = state_->getCurrent(trackIndex);
        if (!entry) return true;

        spine::Animation* anim = entry->getAnimation();
        if (!anim) return true;

        return std::strcmp(anim->getName().buffer(), name) == 0 && entry->isComplete();
    }


} // namespace HIKARI
