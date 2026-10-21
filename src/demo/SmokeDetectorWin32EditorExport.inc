void writeEditorObjAndState(const std::filesystem::path& modelDir) {
    if (!gScene) return;
    std::filesystem::create_directories(modelDir);
    std::filesystem::create_directories(modelDir.parent_path() / "textures");

    auto safeEditorName = [](std::string value) {
        for (char& c : value) {
            const unsigned char u = static_cast<unsigned char>(c);
            if (!(std::isalnum(u) || c == '-' || c == '_' || c == '.')) c = '_';
        }
        return value.empty() ? std::string("asset") : value;
    };

    // Bake image layers into portable PNGs. Materials without an image layer
    // continue to use the TGA created by AssetExporter.
    std::unordered_map<std::string, std::string> textureFiles;
    const std::size_t oldMaterial = gEditorMaterial;
    auto& layers = currentImageLayers();
    for (std::size_t i = 0; i < gScene->materials().size(); ++i) {
        const auto& material = gScene->materials()[i];
        if (material.textureName.empty()) continue;
        if (i < layers.size() && !layers[i].path.empty()) {
            gEditorMaterial = i;
            gLayerBitmap.reset();
            gLayerBitmapPath.clear();
            auto bitmap = compositeTextureBitmap();
            if (bitmap) {
                const std::string filename = safeEditorName(material.textureName) + ".png";
                if (savePng(*bitmap, modelDir.parent_path() / "textures" / filename)) {
                    textureFiles[material.textureName] = filename;
                    continue;
                }
            }
        }
        textureFiles[material.textureName] = safeEditorName(material.textureName) + ".tga";
    }
    gEditorMaterial = oldMaterial;
    gLayerBitmap.reset();
    gLayerBitmapPath.clear();

    std::ofstream obj(modelDir / "model.obj");
    std::ofstream mtl(modelDir / "model.mtl");
    if (!obj || !mtl) throw std::runtime_error("Could not write edited OBJ");
    obj << "mtllib model.mtl\n";

    const auto matrices = editorSkinMatrices();
    const auto& meshEdits = currentMeshEdits();
    std::size_t base = 1;
    for (std::size_t mi = 0; mi < gScene->meshes().size(); ++mi) {
        if (mi < meshEdits.size() && !meshEdits[mi].visible) continue;
        const auto& mesh = gScene->meshes()[mi];
        const MeshEdit edit = mi < meshEdits.size() ? meshEdits[mi] : MeshEdit{};
        const auto pivot = meshCenter(mesh, matrices);
        const Material* material = mesh.materialId < gScene->materials().size()
            ? &gScene->materials()[mesh.materialId] : nullptr;
        const UvFix* fix = uvFixForMaterial(mesh.materialId);
        const std::string materialName = safeEditorName(material && !material->name.empty()
            ? material->name : "material_" + std::to_string(mi));

        obj << "o " << safeEditorName(mesh.name.empty() ? "mesh_" + std::to_string(mi) : mesh.name) << "\n";
        obj << "usemtl " << materialName << "\n";
        for (const auto& vertex : mesh.vertices) {
            auto p = gScene->skin(vertex, matrices);
            p = applyMeshEdit(p, edit, pivot);
            obj << "v " << p.x << ' ' << p.y << ' ' << p.z << "\n";
        }
        for (const auto& vertex : mesh.vertices) {
            const auto uv = adjustedUv(material, vertex.uv, fix);
            obj << "vt " << uv.x << ' ' << (1.0f - uv.y) << "\n";
        }
        for (const auto& vertex : mesh.vertices) {
            obj << "vn " << vertex.normal.x << ' ' << vertex.normal.y << ' ' << vertex.normal.z << "\n";
        }
        for (std::size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
            obj << "f";
            for (int k = 0; k < 3; ++k) {
                const std::size_t n = base + i + static_cast<std::size_t>(k);
                obj << ' ' << n << '/' << n << '/' << n;
            }
            obj << "\n";
        }

        mtl << "newmtl " << materialName << "\nKd 1 1 1\nKa 0 0 0\nKs 0 0 0\n";
        if (material && !material->textureName.empty()) {
            const auto it = textureFiles.find(material->textureName);
            if (it != textureFiles.end()) mtl << "map_Kd ../textures/" << it->second << "\n";
        }
        mtl << "\n";
        base += mesh.vertices.size();
    }

    std::ofstream state(modelDir / "editor-state.json");
    if (!state) return;
    state << "{\n  \"model\": \"" << (gScene->modelName().empty() ? "model" : gScene->modelName()) << "\",\n";
    state << "  \"meshes\": [\n";
    for (std::size_t i = 0; i < meshEdits.size(); ++i) {
        const auto& e = meshEdits[i];
        state << "    {\"index\":" << i << ",\"visible\":" << (e.visible ? "true" : "false")
              << ",\"position\":[" << e.tx << ',' << e.ty << ',' << e.tz << ']'
              << ",\"rotation\":[" << e.rx << ',' << e.ry << ',' << e.rz << ']'
              << ",\"scale\":[" << e.sx << ',' << e.sy << ',' << e.sz << "]}"
              << (i + 1 < meshEdits.size() ? "," : "") << "\n";
    }
    state << "  ],\n  \"bones\": [\n";
    const auto& boneEdits = currentBoneEdits();
    for (std::size_t i = 0; i < boneEdits.size(); ++i) {
        const auto& e = boneEdits[i];
        const std::string name = i < gScene->bones().size() ? gScene->bones()[i].name : std::string{};
        state << "    {\"index\":" << i << ",\"name\":\"" << safeEditorName(name)
              << "\",\"position\":[" << e.tx << ',' << e.ty << ',' << e.tz << ']'
              << ",\"rotation\":[" << e.rx << ',' << e.ry << ',' << e.rz << "]}"
              << (i + 1 < boneEdits.size() ? "," : "") << "\n";
    }
    state << "  ],\n  \"materials\": [\n";
    const auto& fixes = currentUvFixes();
    for (std::size_t i = 0; i < gScene->materials().size(); ++i) {
        const auto& material = gScene->materials()[i];
        const UvFix fix = i < fixes.size() ? fixes[i] : UvFix{};
        const ImageLayerState layer = i < layers.size() ? layers[i] : ImageLayerState{};
        state << "    {\"index\":" << i << ",\"name\":\"" << safeEditorName(material.name)
              << "\",\"texture\":\"" << safeEditorName(material.textureName)
              << "\",\"uv\":{\"transform\":" << fix.transformType
              << ",\"offset\":[" << fix.offsetU << ',' << fix.offsetV << ']'
              << ",\"scale\":[" << fix.scaleU << ',' << fix.scaleV << ']'
              << ",\"rotation\":" << fix.rotation << "},\"image_layer\":{\"path\":\""
              << safeEditorName(std::filesystem::path(layer.path).filename().string())
              << "\",\"offset\":[" << layer.offsetU << ',' << layer.offsetV << ']'
              << ",\"scale\":" << layer.scale << ",\"rotation\":" << layer.rotation
              << ",\"crop\":[" << layer.cropX << ',' << layer.cropY << ',' << layer.cropW << ',' << layer.cropH << "]}}"
              << (i + 1 < gScene->materials().size() ? "," : "") << "\n";
    }
    state << "  ]\n}\n";
}

void exportEditorSelected() {
    if (gSelectedItem >= gItems.size()) return;
    const auto& item = gItems[gSelectedItem];
    const std::string model = item.category == 1 ? modelPathFor(item) : std::string{};
    if (model.empty() || !gArchive || !gScene || gSelectedModel != model) {
        exportStudioSelected();
        return;
    }

    const auto result = AssetExporter::exportModel(*gArchive, model, gExportRoot);
    writeEditorObjAndState(result.interchangeModel.parent_path());
    setStatus("Edited model exported: " + result.interchangeModel.string());
    ShellExecuteA(gWindow, "open", result.interchangeModel.parent_path().string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
