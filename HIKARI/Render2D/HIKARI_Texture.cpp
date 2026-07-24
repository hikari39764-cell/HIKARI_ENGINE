#include "Render2D/HIKARI_Texture.h"
#include "Render2D/HIKARI_DxTexture.h"


namespace HIKARI {
	namespace TEXTURE {


		static std::vector<Entry> gEntries;
		static std::unordered_map<std::string, int> gNameToId;


		void Register(const std::string& name, const std::string& path, const std::string& group) {
			if (gNameToId.find(name) != gNameToId.end()) { return; }

			Entry e;
			e.name = name;
			e.path = path;
			e.group = group;
			e.handle = -1; // Novice
			e.dxHandle = -1; // DX

			int id = static_cast<int>(gEntries.size());
			gEntries.push_back(e);
			gNameToId[name] = id;
		}



		bool LoadGroup(const std::string& group) {
			bool ok = true;

			for (auto& e : gEntries) {
				if (e.group != group) {
					continue;
				}

				e.dxHandle = GetDxHandle(e.name);
				if (e.dxHandle < 0) {
					ok = false;
				}
			}

			return ok;
		}


		bool LoadAll() {
			bool ok = true;

			for (auto& e : gEntries) {

				//// Novice â€šÃˁE
				//if (e.handle < 0) {
				//	e.handle = Novice::LoadTexture(e.path.c_str());
				//	if (e.handle < 0) {
				//		ok = false;
				//	}
				//}

				// DX â€šÃˁE
				if (e.dxHandle < 0) {
					int dxH = HIKARI::DXTEX::DxTextureManager::LoadTexture(e.name, e.path);
					if (dxH < 0) {
						ok = false;
					}
					e.dxHandle = dxH;
				}
			}

			return ok;
		}




		int GetDxHandle(const std::string& name) {
			auto it = gNameToId.find(name);
			if (it == gNameToId.end()) {
				return -1;
			}

			int id = it->second;
			Entry& e = gEntries[id];

			if (e.dxHandle < 0) {

				e.dxHandle = HIKARI::DXTEX::DxTextureManager::LoadTexture(e.name, e.path);
			}
			return e.dxHandle;
		}


		void UnloadAll() {
			for (auto& e : gEntries) {
				e.handle = -1;
				e.dxHandle = -1;
			}
		}



	}
} // namespace HIKARI::TEXTURE