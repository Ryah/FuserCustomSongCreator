#include "uasset.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "crc.h"
#include <optional>
#include <algorithm>
#include <array>

#include <filesystem>
namespace fs = std::filesystem;

#include "custom_song_pak_template.h"

#define NOMINMAX
#include <Windows.h>

#include "fuser_asset.h"

struct ImGuiErrorModalManager {
	size_t error_id = 0;

	struct Error {
		size_t id;
		std::string message;
	};
	std::vector<Error> errors;

	std::string getErrorName(size_t id) {
		std::string name = "";
		name += "Error_" + std::to_string(id);
		return name;
	}

	void pushError(std::string error) {
		Error e;
		e.id = error_id++;
		e.message = error;
		ImGui::OpenPopup(getErrorName(e.id).c_str());
		errors.emplace_back(e);
	}

	void update() {

		for(auto it = errors.begin(); it != errors.end();) {
			auto &&e = *it;
			bool remove = false;

			if (ImGui::BeginPopupModal(getErrorName(e.id).c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::Text(e.message.c_str());
				ImGui::Separator();

				if (ImGui::Button("OK", ImVec2(120, 0))) {
					ImGui::CloseCurrentPopup(); 
					remove = true;
				}
				ImGui::SetItemDefaultFocus();
			}

			if (remove) {
				it = errors.erase(it);
			}
			else {
				++it;
			}
		}
	}
};
ImGuiErrorModalManager errorManager;

static void ErrorModal(const char *name, const char *msg) {
	if (ImGui::BeginPopupModal(name, NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text(msg);
		ImGui::Separator();

		if (ImGui::Button("OK", ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemDefaultFocus();

		ImGui::EndPopup();
	}
}

static void HelpMarker(const char* desc)
{
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(desc);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

template<typename T>
static void ChooseFuserEnum(const char *label, std::string &out) {
	auto&& values = T::GetValues();

	auto getter = [](void *data, int idx, const char **out_str) {
		auto &&d = reinterpret_cast<std::vector<std::string>*>(data);
		*out_str = (*d)[idx].c_str();
		return true;
	};

	int currentChoice = 0;
	for (size_t i = 0; i < values.size(); ++i) {
		if (values[i] == out) {
			currentChoice = i;
			break;
		}
	}

	if (ImGui::Combo(label, &currentChoice, getter, &values, values.size())) {
		out = values[currentChoice];
	}
}

static std::optional<std::string> OpenFile(LPCSTR filter) {
	CHAR szFileName[MAX_PATH];

	// open a file name
	OPENFILENAME ofn;
	ZeroMemory(&szFileName, sizeof(szFileName));
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFile = szFileName;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(szFileName);
	ofn.lpstrFilter = filter;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
	if (GetOpenFileNameA(&ofn)) {
		return std::string(ofn.lpstrFile);
	}

	return std::nullopt;
}

static std::optional<std::string> SaveFile(LPCSTR filter, const std::string &fileName) {
	CHAR szFileName[MAX_PATH];

	// open a file name
	OPENFILENAME ofn;
	ZeroMemory(&szFileName, sizeof(szFileName));
	ZeroMemory(&ofn, sizeof(ofn));
	memcpy(szFileName, fileName.data(), std::min(fileName.size(), (size_t)MAX_PATH));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFile = szFileName;
	ofn.nMaxFile = sizeof(szFileName);
	ofn.lpstrFilter = filter;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.lpstrDefExt = "pak";
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
	if (GetSaveFileName(&ofn)) {
		return std::string(ofn.lpstrFile);
	}

	return std::nullopt;
}

struct MainContext {
	std::string saveLocation;

	struct CurrentPak {
		PakFile pak;
		AssetRoot root;
	};
	std::unique_ptr<CurrentPak> currentPak;

};
MainContext gCtx;

void load_file(DataBuffer &&dataBuf) {
	gCtx.currentPak = std::make_unique<MainContext::CurrentPak>();
	gCtx.saveLocation.clear();

	auto &&pak = gCtx.currentPak->pak;

	dataBuf.serialize(pak);

	for (auto &&e : pak.entries) {
		if (auto data = std::get_if<PakFile::PakEntry::PakAssetData>(&e.data)) {
			auto pos = e.name.find("DLC/Songs/");
			if (pos != std::string::npos) {
				std::string shortName;
				for (size_t i = 10; i < e.name.size(); ++i) {
					if (e.name[i] == '/') {
						break;
					}

					shortName += e.name[i];
				}

				//Double check we got the name correct
				if (e.name == ("DLC/Songs/" + shortName + "/Meta_" + shortName + ".uexp")) {
					gCtx.currentPak->root.shortName = shortName;
					break;
				}
			}
		}
	}

	if (gCtx.currentPak->root.shortName.empty()) {
		printf("FATAL ERROR! No short name detected!");
		__debugbreak();
	}

	SongSerializationCtx ctx;
	ctx.loading = true;
	ctx.pak = &pak;
	gCtx.currentPak->root.serialize(ctx);
}

void load_template() {
	DataBuffer dataBuf;
	dataBuf.buffer = (u8*)custom_song_pak_template;
	dataBuf.size = sizeof(custom_song_pak_template);
	load_file(std::move(dataBuf));
}

void save_file() {
	SongSerializationCtx ctx;
	ctx.loading = false;
	ctx.pak = &gCtx.currentPak->pak;
	gCtx.currentPak->root.serialize(ctx);

	std::vector<u8> outData;
	DataBuffer outBuf;
	outBuf.setupVector(outData);
	outBuf.loading = false;
	gCtx.currentPak->pak.serialize(outBuf);
	outBuf.finalize();

	std::string basePath = fs::path(gCtx.saveLocation).parent_path().string();
	std::ofstream outPak(basePath + gCtx.currentPak->root.shortName + "_P.pak", std::ios_base::binary);
	outPak.write((char*)outBuf.buffer, outBuf.size);

	{
		PakSigFile sigFile;
		sigFile.encrypted_total_hash.resize(512);

		const u32 chunkSize = 64 * 1024;
		for (size_t start = 0; start < outBuf.size; start += chunkSize) {
			size_t end = start + chunkSize;
			if (end > outBuf.size) {
				end = outBuf.size;
			}

			sigFile.chunks.emplace_back(CRC::MemCrc32(outBuf.buffer + start, end - start));
		}

		std::vector<u8> sigOutData;
		DataBuffer sigOutBuf;
		sigOutBuf.setupVector(sigOutData);
		sigOutBuf.loading = false;
		sigFile.serialize(sigOutBuf);
		sigOutBuf.finalize();

		std::ofstream outPak(basePath + gCtx.currentPak->root.shortName + "_P.sig", std::ios_base::binary);
		outPak.write((char*)sigOutBuf.buffer, sigOutBuf.size);
	}
}

bool Error_InvalidFileName = false;
void select_save_location() {
	auto fileName = gCtx.currentPak->root.shortName + "_P.pak";
	auto location = SaveFile("Fuser Custom Song (*.pak)\0*.pak\0", fileName);
	if (location) {
		auto path = fs::path(*location);
		auto savedFileName = path.stem().string() + path.extension().string();
		if (savedFileName != fileName) {
			Error_InvalidFileName = true;
		}
		else {
			gCtx.saveLocation = *location;
			save_file();
		}
	}
}

static int ValidateShortName(ImGuiInputTextCallbackData* data) {
	if (!isalnum(data->EventChar) && data->EventChar != '_') {
		return 1;
	}

	return 0;
}

void display_main_properties() {
	auto &&root = gCtx.currentPak->root;

	if (ImGui::InputText("Short Name", &root.shortName, ImGuiInputTextFlags_CallbackCharFilter, ValidateShortName)) {
		gCtx.saveLocation.clear(); //We clear the save location, since it needs to resolve to another file path.
	}

	ImGui::SameLine();
	HelpMarker("Short name can only contain alphanumeric characters and '_'. This name is used to uniquely identify your custom song.");

	ImGui::InputText("Song Name", &root.songName);
	ImGui::InputText("Artist Name", &root.artistName);

	ImGui::InputScalar("BPM", ImGuiDataType_S32, &root.bpm);
	ChooseFuserEnum<FuserEnums::Key>("Key", root.songKey);
}

void display_cell_data(CelData &celData) {
	auto &&fusionFile = celData.majorAssets[0].data.fusionFile.data;
	auto &&asset = std::get<HmxAssetFile>(fusionFile.file.e->getData().data.catagoryValues[0].value);
	auto &&mogg = asset.audio.audioFiles[0];
	auto &&header = std::get<HmxAudio::PackageFile::MoggSampleResourceHeader>(mogg.resourceHeader);

	if (ImGui::Button("Replace Audio")) {
		auto moggFile = OpenFile("Encrypted Mogg (*.mogg)\0*.mogg\0");
		if (moggFile) {
			std::ifstream infile(*moggFile, std::ios_base::binary);
			std::vector<u8> fileData = std::vector<u8>(std::istreambuf_iterator<char>(infile), std::istreambuf_iterator<char>());
			if (fileData.size() > 0 && fileData[0] == 0x0B) {
				mogg.fileData = std::move(fileData);
			}
			else {
				ImGui::OpenPopup("Encrypted Mogg Error");
			}
		}
	}

	ImGui::InputScalar("Sample Rate", ImGuiDataType_U32, &header.sample_rate);

	ChooseFuserEnum<FuserEnums::Instrument>("Instrument", celData.instrument);
	//ChooseFuserEnum<FuserEnums::Instrument>("Sub Instrument", celData.subInstrument);

	ErrorModal("Encrypted Mogg Error", "Invalid mogg file! The mogg was probably un-encrypted, make sure to run it through MoggcryptCpp first!");
}

void custom_song_creator_update(size_t width, size_t height) {
	bool do_open = false;
	bool do_save = false;

	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New")) {
				load_template();
			}
			if (ImGui::MenuItem("Open", "Ctrl+O")) {
				do_open = true;
			}

			if (ImGui::MenuItem("Save", "Ctrl+S")) {
				do_save = true;
			}

			if (ImGui::MenuItem("Save As..")) {
				select_save_location();
			}

			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}

	auto &&input = ImGui::GetIO();

	if (input.KeyCtrl) {
		if (input.KeysDown['O'] && input.KeysDownDuration['O'] == 0.0f) {
			do_open = true;
		}
		if (input.KeysDown['S'] && input.KeysDownDuration['S'] == 0.0f) {
			do_save = true;
		}
	}


	if (do_open) {
		auto file = OpenFile("Fuser Custom Song (*.pak)\0*.pak\0");
		if (file) {
			std::ifstream infile(*file, std::ios_base::binary);
			std::vector<u8> fileData = std::vector<u8>(std::istreambuf_iterator<char>(infile), std::istreambuf_iterator<char>());

			DataBuffer dataBuf;
			dataBuf.setupVector(fileData);
			load_file(std::move(dataBuf));

			gCtx.saveLocation = *file;
		}
	}

	if (do_save) {
		if (!gCtx.saveLocation.empty()) {
			save_file();
		}
		else {
			select_save_location();
		}
	}

	ImGui::SetNextWindowPos(ImVec2{ 0, ImGui::GetFrameHeight() });
	ImGui::SetNextWindowSize(ImVec2{ (float)width, (float)height - ImGui::GetFrameHeight() });

	ImGuiWindowFlags window_flags = 0;
	window_flags |= ImGuiWindowFlags_NoTitleBar;
	window_flags |= ImGuiWindowFlags_NoMove;
	window_flags |= ImGuiWindowFlags_NoResize;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

	ImGui::Begin("Fuser Custom Song Creator", nullptr, window_flags);
	
	if (gCtx.currentPak != nullptr) {
		if (ImGui::BeginTabBar("Tabs")) {

			if (ImGui::BeginTabItem("Main Properties")) {
				display_main_properties();

				ImGui::EndTabItem();
			}

			for (auto &&cel : gCtx.currentPak->root.celData) {
				std::string tabName = "Song Cell - ";
				tabName += cel.data.type.getString();
				if (ImGui::BeginTabItem(tabName.c_str())) {
					display_cell_data(cel.data);
					ImGui::EndTabItem();
				}
			}

			ImGui::EndTabBar();
		}

		if (Error_InvalidFileName) {
			ImGui::OpenPopup("Invalid File Name");
			Error_InvalidFileName = false;
		}
		auto fileName = gCtx.currentPak->root.shortName + "_P.pak";
		auto error = "Your file must be named as " + fileName + ", otherwise the song loader won't unlock it!";
		ErrorModal("Invalid File Name", error.c_str());
	}
	else {
		ImGui::Text("Welcome to the Fuser Custom Song Creator!");
		ImGui::Text("To get started with the default template, choose File -> New from the menu, or use the button:"); ImGui::SameLine();
		if (ImGui::Button("Create New Custom Song")) {
			load_template();
		}

		ImGui::Text("To open an existing custom song, use File -> Open.");
	}

	ImGui::End();
	ImGui::PopStyleVar();
}