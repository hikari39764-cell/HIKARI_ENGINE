#pragma once
#include <string>
#include <vector>
#include <unordered_map>

namespace HIKARI {
	namespace TEXTURE {

		struct Entry {
			std::string name;  // ユニークな名前
			std::string path;  // ファイルパス
			std::string group; // グループ分け
			int handle{ -1 };    // Novice のハンドル（旧描画用）
			int dxHandle{ -1 };  // DX のテクスチャハンドル（DxTextureManager 用）
		};

		// name / path / group を登録する
		void Register(const std::string& name, const std::string& path, const std::string& group = "default");

		// 指定した group のテクスチャを読み込む（シーンごとに呼び出し可能）
		bool LoadGroup(const std::string& group);

		// 登録されている全テクスチャを読み込む
		bool LoadAll();

		// 名称から handle を取得（必要なら遅延読み込み）
		int GetDxHandle(const std::string& name);
		//すべて解放
		void UnloadAll();

	}
} // namespace HIKARI::TEXTURE
