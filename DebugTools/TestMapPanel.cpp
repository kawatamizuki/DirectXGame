#include "TestMapPanel.h"
#include <cstdio>
#include <cstring>
#include "imgui.h"
#include "TestMapSession.h"

namespace
{
    const ImVec4 kColorOfficial(0.45f, 0.90f, 0.50f, 1.0f);
    const ImVec4 kColorDraft(1.00f, 0.85f, 0.30f, 1.0f);
    const ImVec4 kColorError(1.00f, 0.45f, 0.40f, 1.0f);
    const ImVec4 kColorNote(0.62f, 0.62f, 0.62f, 1.0f);
    const ImVec4 kColorHeading(0.55f, 0.80f, 1.00f, 1.0f);

    void Heading(const char* text)
    {
        ImGui::Spacing();
        ImGui::TextColored(kColorHeading, "%s", text);
        ImGui::Separator();
    }

    const char* FolderName(TestMapFolder folder) { return folder == TestMapFolder::Official ? "正式" : "下書き"; }
    const ImVec4& FolderColor(TestMapFolder folder) { return folder == TestMapFolder::Official ? kColorOfficial : kColorDraft; }

    void CopyTo(char* destination, size_t size, const std::string& text)
    {
        std::snprintf(destination, size, "%s", text.c_str());
    }

    // 「注目する指標」の、1つの文字列(区切りは、「、」「,」「改行」)を、並びにする。前後の空白は落とす。
    std::vector<std::string> SplitMetrics(const char* text)
    {
        std::vector<std::string> result;
        std::string current;
        auto flush = [&]()
        {
            size_t begin = 0, end = current.size();
            while (begin < end && (current[begin] == ' ' || current[begin] == '\t')) { ++begin; }
            while (end > begin && (current[end - 1] == ' ' || current[end - 1] == '\t')) { --end; }
            if (end > begin) { result.push_back(current.substr(begin, end - begin)); }
            current.clear();
        };
        const std::string source = text;
        for (size_t i = 0; i < source.size(); ++i)
        {
            if (source[i] == ',' || source[i] == '\n' || source[i] == '\r') { flush(); }
            else if (source.compare(i, 3, "、") == 0) { flush(); i += 2; }
            else { current += source[i]; }
        }
        flush();
        return result;
    }

    std::string JoinMetrics(const std::vector<std::string>& metrics)
    {
        std::string text;
        for (size_t i = 0; i < metrics.size(); ++i) { text += (i ? "、" : "") + metrics[i]; }
        return text;
    }

    std::string ShortHash(std::uint64_t value)
    {
        char text[24];
        std::snprintf(text, sizeof(text), "%08llX", static_cast<unsigned long long>(value >> 32));
        return text;
    }
}

void TestMapPanel::SetMessage(const std::string& text, bool error)
{
    m_message = text;
    m_messageIsError = error;
}

void TestMapPanel::Refresh(TestMapStore& store)
{
    m_entries = store.List();
    m_dirty = false;
    m_lastRefresh = ImGui::GetTime();
    if (!m_selected.empty() && !FindEntry(m_selected)) { m_selected.clear(); }
}

const TestMapEntry* TestMapPanel::FindEntry(const std::string& id) const
{
    for (const TestMapEntry& entry : m_entries)
    {
        if (entry.id == id) { return &entry; }
    }
    return nullptr;
}

void TestMapPanel::Draw(TestMapStore& store, TestMapSession& session, bool canOperate, const char* whyNot)
{
    if (m_open && !*m_open)
    {
        return;
    }
    if (!ImGui::Begin("テストマップ", m_open))
    {
        ImGui::End();
        return;
    }
    if (m_dirty || ImGui::GetTime() - m_lastRefresh > 2.0)
    {
        Refresh(store);
    }

    ImGui::TextWrapped("手作業で作った街を、名前を付けて保存し、いつでも、同じ初期状態から読み込みます。読み込んだ街は、通常どおり、建築・編集・時間進行できます。"
                       "元のテストマップのファイルは、変わりません(通常のセーブ・F5は、Saves/ へ保存します)。");
    if (!canOperate && whyNot && whyNot[0] != '\0')
    {
        ImGui::TextColored(kColorDraft, "%s", whyNot);
    }

    DrawActive(store, session, canOperate);

    Heading("保存済みのテストマップ");
    if (ImGui::Button("一覧を更新")) { m_dirty = true; }
    ImGui::SameLine();
    ImGui::TextDisabled("正式: TestMaps/(Git管理) / 下書き: TestMaps/Drafts/(Git管理外)");
    DrawList();

    if (const TestMapEntry* entry = FindEntry(m_selected))
    {
        DrawDetail(*entry);
        DrawActions(store, session, *entry, canOperate);
    }
    else
    {
        ImGui::TextDisabled("一覧から、テストマップを選ぶと、内容と操作が出ます。");
    }

    DrawSaveNew(store, session, canOperate);

    if (!m_message.empty())
    {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(m_messageIsError ? kColorError : kColorNote, "%s", m_message.c_str());
    }

    DrawPopups(store, session);
    ImGui::End();
}

void TestMapPanel::DrawActive(TestMapStore& store, TestMapSession& session, bool canOperate)
{
    (void)store;
    Heading("使用中");
    const TestMapStore::ActiveMap& active = session.Active();
    if (active.valid)
    {
        const TestMapEntry* entry = FindEntry(active.id);
        ImGui::Text("テストマップ: ");
        ImGui::SameLine();
        ImGui::TextColored(entry ? FolderColor(entry->folder) : kColorNote, "%s", active.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(%s / %s / 指紋 %s)", active.id.c_str(), entry ? FolderName(entry->folder) : "?", ShortHash(active.fingerprint).c_str());

        // 配置が、読み込んだ時と違うか。街を書き出して調べるので、間引く。
        if (ImGui::GetTime() - m_lastModifiedCheck > 1.5)
        {
            bool modified = false;
            m_modifiedKnown = session.IsLayoutModified(modified);
            m_modified = modified;
            m_lastModifiedCheck = ImGui::GetTime();
        }
        if (m_modifiedKnown)
        {
            if (m_modified) { ImGui::TextColored(kColorDraft, "配置を変えています(建築・編集)。元のテストマップのファイルは、変わっていません。"); }
            else { ImGui::TextColored(kColorOfficial, "配置は、元のテストマップと、同じです。"); }
        }
        // この判定が、何を比べているか。街の全状態(時間経過・住民・需要・評判・資金)の比較ではない。
        ImGui::TextDisabled("※「配置」は、道路・建物・青マス・飾り・商業エリアの形(位置・大きさ・周知の有無)だけの比較です。"
                            "時間経過・住民の状態・需要・評判・資金などが変わっていても、「同じ」と出ます(街の全状態の比較ではありません)。");
        ImGui::TextColored(kColorOfficial, "F5/F9(メニューのセーブ・ロード)は、テスト用のクイックセーブ(%s)を使います。普段のクイックセーブ(Saves/quicksave.json)は、変わりません。",
                           TestMapSession::kQuickSavePath);
    }
    else
    {
        ImGui::TextDisabled("テストマップは、使っていません。");
        m_modifiedKnown = false;
    }

    ImGui::BeginDisabled(!session.HasOriginal() || !canOperate);
    if (ImGui::Button("元の街に戻す…"))
    {
        m_popup = Popup::Restore;
        m_openPopupRequest = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled(session.HasOriginal() ? "テストマップを読み込む前の街が、メモリに控えてあります" : "(読み込む前の街の控えは、ありません)");
    if (active.valid)
    {
        ImGui::BeginDisabled(!canOperate);
        if (ImGui::Button("テストマップの使用をやめる…"))
        {
            m_popup = Popup::Abandon;
            m_openPopupRequest = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("(今の街は、そのまま続けます)");
    }
}

void TestMapPanel::DrawList()
{
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##testmaps", 4, flags, ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 8.0f)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("種別", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("表示名");
        ImGui::TableSetupColumn("id");
        ImGui::TableSetupColumn("作成日時");
        ImGui::TableHeadersRow();
        for (const TestMapEntry& entry : m_entries)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(entry.id.c_str());
            ImGui::TextColored(FolderColor(entry.folder), "%s", FolderName(entry.folder));
            ImGui::TableNextColumn();
            const bool selected = (entry.id == m_selected);
            const std::string label = (entry.valid ? entry.meta.name : std::string("(読めません) ") + entry.id) + "##row";
            if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) { m_selected = entry.id; }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry.id.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry.valid ? entry.meta.createdAt.c_str() : "-");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (m_entries.empty())
    {
        ImGui::TextDisabled("まだ、テストマップがありません。街を作ってから、下の「今の街を、新しいテストマップとして保存」で、保存してください。");
    }
}

void TestMapPanel::DrawDetail(const TestMapEntry& entry)
{
    Heading("内容");
    ImGui::TextColored(FolderColor(entry.folder), "[%s]", FolderName(entry.folder));
    ImGui::SameLine();
    ImGui::Text("%s  (%s)", entry.valid ? entry.meta.name.c_str() : entry.id.c_str(), entry.id.c_str());
    if (!entry.valid)
    {
        ImGui::TextColored(kColorError, "読み込めません: %s", entry.error.c_str());
        ImGui::TextDisabled("ファイル: %s", entry.path.c_str());
        return;
    }
    auto field = [](const char* label, const std::string& text)
    {
        ImGui::TextColored(kColorNote, "%s", label);
        ImGui::SameLine();
        if (text.empty()) { ImGui::TextDisabled("(未記入)"); }
        else { ImGui::TextWrapped("%s", text.c_str()); }
    };
    field("説明:", entry.meta.description);
    field("検証目的:", entry.meta.purpose);
    field("注目する指標:", JoinMetrics(entry.meta.focusMetrics));
    ImGui::TextDisabled("作成: %s / ビルド: %s", entry.meta.createdAt.c_str(), entry.meta.gameBuild.c_str());
    ImGui::TextDisabled("初期状態の指紋: %s %s / %.1f KB", ShortHash(entry.fingerprint).c_str(), "(街の内容。説明を直しても、変わりません)", static_cast<double>(entry.sizeBytes) / 1024.0);
    ImGui::TextDisabled("ファイル: %s", entry.path.c_str());
}

void TestMapPanel::DrawActions(TestMapStore& store, TestMapSession& session, const TestMapEntry& entry, bool canOperate)
{
    (void)store;
    ImGui::Spacing();
    ImGui::BeginDisabled(!entry.valid || !canOperate);
    if (ImGui::Button("読み込む…"))
    {
        m_popup = Popup::Load;
        m_openPopupRequest = true;
        m_popupTarget = entry.id;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!entry.valid);
    if (ImGui::Button("説明・検証目的を編集…"))
    {
        m_popup = Popup::Edit;
        m_openPopupRequest = true;
        m_popupTarget = entry.id;
        CopyTo(m_editName, sizeof(m_editName), entry.meta.name);
        CopyTo(m_editDescription, sizeof(m_editDescription), entry.meta.description);
        CopyTo(m_editPurpose, sizeof(m_editPurpose), entry.meta.purpose);
        CopyTo(m_editMetrics, sizeof(m_editMetrics), JoinMetrics(entry.meta.focusMetrics));
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("id(ファイル名)を変更…"))
    {
        m_popup = Popup::Rename;
        m_openPopupRequest = true;
        m_popupTarget = entry.id;
        CopyTo(m_renameId, sizeof(m_renameId), entry.id);
    }
    ImGui::SameLine();
    if (entry.folder == TestMapFolder::Draft)
    {
        if (ImGui::Button("正式に採用…")) { m_popup = Popup::Promote; m_openPopupRequest = true; m_popupTarget = entry.id; }
    }
    else
    {
        if (ImGui::Button("下書きへ戻す…")) { m_popup = Popup::Demote; m_openPopupRequest = true; m_popupTarget = entry.id; }
    }
    ImGui::SameLine();
    if (ImGui::Button("削除…"))
    {
        m_popup = Popup::Delete;
        m_openPopupRequest = true;
        m_popupTarget = entry.id;
        m_deleteConfirm[0] = '\0';
    }
    (void)session;
}

void TestMapPanel::DrawSaveNew(TestMapStore& store, TestMapSession& session, bool canOperate)
{
    Heading("今の街を、新しいテストマップとして保存");
    if (!ImGui::CollapsingHeader("保存する(新しい名前だけ。既存のテストマップは、上書きされません)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }
    if (session.Active().valid)
    {
        ImGui::TextDisabled("今の街は、テストマップ「%s」から、始まっています。保存しても、元のテストマップは、変わりません(別の名前の、新しいテストマップになります)。", session.Active().name.c_str());
    }
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("id(ファイル名。半角英数字・_・-)", m_newId, sizeof(m_newId));
    std::string why;
    const bool idOk = TestMapStore::IsValidId(m_newId, &why);
    if (m_newId[0] != '\0' && !idOk) { ImGui::TextColored(kColorError, "%s", why.c_str()); }
    else if (idOk)
    {
        if (FindEntry(m_newId)) { ImGui::TextColored(kColorError, "同じ名前が、既にあります(上書きはしません)。別の名前にしてください。"); }
    }
    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputText("表示名(日本語可)", m_newName, sizeof(m_newName));
    ImGui::InputTextMultiline("説明(どんな街か)", m_newDescription, sizeof(m_newDescription), ImVec2(-1.0f, ImGui::GetTextLineHeight() * 3.5f));
    ImGui::InputTextMultiline("検証目的(何を確かめる街か)", m_newPurpose, sizeof(m_newPurpose), ImVec2(-1.0f, ImGui::GetTextLineHeight() * 3.5f));
    ImGui::SetNextItemWidth(420.0f);
    ImGui::InputText("注目する指標(「、」区切り)", m_newMetrics, sizeof(m_newMetrics));
    ImGui::RadioButton("下書き(既定。Git管理外)", &m_newFolder, 0);
    ImGui::SameLine();
    ImGui::RadioButton("正式(Git管理)", &m_newFolder, 1);

    const bool exists = idOk && FindEntry(m_newId) != nullptr;
    ImGui::BeginDisabled(!idOk || exists || !canOperate);
    if (ImGui::Button("保存する"))
    {
        TestMapMeta meta;
        meta.name = m_newName;
        meta.description = m_newDescription;
        meta.purpose = m_newPurpose;
        meta.focusMetrics = SplitMetrics(m_newMetrics);
        std::string message;
        const bool ok = store.SaveNew(m_newId, meta, m_newFolder == 1 ? TestMapFolder::Official : TestMapFolder::Draft, message);
        SetMessage(message, !ok);
        if (ok)
        {
            m_selected = m_newId;
            m_newId[0] = m_newName[0] = m_newDescription[0] = m_newPurpose[0] = m_newMetrics[0] = '\0';
            m_dirty = true;
        }
    }
    ImGui::EndDisabled();
}

void TestMapPanel::DrawPopups(TestMapStore& store, TestMapSession& session)
{
    auto popupName = [](Popup p) -> const char*
    {
        switch (p)
        {
        case Popup::Load:    return "テストマップを読み込む##tm";
        case Popup::Restore: return "元の街に戻す##tm";
        case Popup::Abandon: return "テストマップの使用をやめる##tm";
        case Popup::Edit:    return "説明・検証目的を編集##tm";
        case Popup::Rename:  return "idを変更##tm";
        case Popup::Promote: return "正式に採用##tm";
        case Popup::Demote:  return "下書きへ戻す##tm";
        case Popup::Delete:  return "テストマップを削除##tm";
        default:             return "";
        }
    };
    if (m_popup == Popup::None)
    {
        return;
    }
    if (m_openPopupRequest)
    {
        ImGui::OpenPopup(popupName(m_popup));
        m_openPopupRequest = false;
    }
    const TestMapEntry* target = FindEntry(m_popupTarget);
    bool close = false;
    if (ImGui::BeginPopupModal(popupName(m_popup), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        switch (m_popup)
        {
        case Popup::Load:
        {
            ImGui::Text("「%s」を、読み込みます。", target ? target->meta.name.c_str() : m_popupTarget.c_str());
            bool modified = false;
            const bool known = session.IsLayoutModified(modified);
            if (session.Active().valid && known && modified)
            {
                ImGui::TextColored(kColorDraft, "今の街は、テストマップ「%s」から、配置が変わっています。読み込むと、その変更は、失われます。", session.Active().name.c_str());
            }
            else if (!session.HasOriginal())
            {
                ImGui::TextWrapped("今の街を、メモリに控えてから、読み込みます。「元の街に戻す」で、戻せます。");
            }
            else
            {
                ImGui::TextWrapped("別のテストマップを読み込みます。「元の街に戻す」で戻れる街は、最初に控えた街のままです。");
            }
            ImGui::TextWrapped("今の街の、時間の経過・住民の状態・需要・評判・資金も、読み込むと、置き換わります(「配置が同じ」でも、これらは、失われます)。");
            ImGui::TextDisabled("元のテストマップのファイルは、変わりません。");
            if (ImGui::Button("読み込む"))
            {
                std::string message;
                const bool ok = session.Load(m_popupTarget, message);
                SetMessage(message, !ok);
                m_modifiedKnown = false;
                m_lastModifiedCheck = -100.0;
                close = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("やめる")) { close = true; }
            break;
        }
        case Popup::Restore:
        {
            ImGui::TextWrapped("テストマップを読み込む前の街へ戻します。読み込んだ後に行った、次の物は、すべて破棄されます:");
            ImGui::BulletText("建築・編集(建物・道路・区画の追加・解体・移動)");
            ImGui::BulletText("時間の経過(日付・週の進み・住民の行動・需要・体力・満足度・売上・資金・商業エリアの評判・各種の集計)");
            ImGui::BulletText("テスト用のクイックセーブ(F5)に残した街は、そのまま残ります(ファイルは、消しません)");
            ImGui::TextWrapped("戻る先は、読み込む前の街の、控えた時点の、全状態です(配置だけではありません)。元のテストマップのファイルは、変わりません。");
            bool modified = false;
            if (session.IsLayoutModified(modified) && modified)
            {
                ImGui::TextColored(kColorDraft, "今の街は、配置も、テストマップから変わっています。");
            }
            if (ImGui::Button("戻す"))
            {
                std::string message;
                const bool ok = session.RestoreOriginal(message);
                SetMessage(message, !ok);
                m_modifiedKnown = false;
                close = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("やめる")) { close = true; }
            break;
        }
        case Popup::Abandon:
        {
            ImGui::TextWrapped("テストマップの使用をやめます。今の街は、そのまま続けます(街は、入れ替わりません)。");
            ImGui::BulletText("読み込む前の街の控えは、捨てます(「元の街に戻す」は、できなくなります)");
            ImGui::BulletText("F5/F9は、普段のクイックセーブ(Saves/quicksave.json)へ戻ります。その後に F5 を押すと、今の街で、普段のクイックセーブが、上書きされます");
            ImGui::TextDisabled("元のテストマップのファイルは、変わりません。");
            if (ImGui::Button("使用をやめる"))
            {
                session.EndSession();
                SetMessage("テストマップの使用をやめました(今の街は、そのまま続きます)", false);
                m_modifiedKnown = false;
                close = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("やめる")) { close = true; }
            break;
        }
        case Popup::Edit:
        {
            ImGui::TextDisabled("街の部分は、変わりません(初期状態の指紋も、変わりません)。");
            ImGui::SetNextItemWidth(320.0f);
            ImGui::InputText("表示名", m_editName, sizeof(m_editName));
            ImGui::InputTextMultiline("説明", m_editDescription, sizeof(m_editDescription), ImVec2(420.0f, ImGui::GetTextLineHeight() * 4.0f));
            ImGui::InputTextMultiline("検証目的", m_editPurpose, sizeof(m_editPurpose), ImVec2(420.0f, ImGui::GetTextLineHeight() * 4.0f));
            ImGui::SetNextItemWidth(420.0f);
            ImGui::InputText("注目する指標(「、」区切り)", m_editMetrics, sizeof(m_editMetrics));
            if (ImGui::Button("保存する"))
            {
                TestMapMeta meta;
                meta.name = m_editName;
                meta.description = m_editDescription;
                meta.purpose = m_editPurpose;
                meta.focusMetrics = SplitMetrics(m_editMetrics);
                std::string message;
                const bool ok = store.UpdateMeta(m_popupTarget, meta, message);
                SetMessage(message, !ok);
                m_dirty = true;
                close = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("やめる")) { close = true; }
            break;
        }
        case Popup::Rename:
        {
            ImGui::Text("現在のid: %s", m_popupTarget.c_str());
            ImGui::TextWrapped("ファイル名を変えます(中身は、変わりません)。同じ名前が既にあれば、変えられません。");
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("新しいid", m_renameId, sizeof(m_renameId));
            std::string why;
            const bool ok = TestMapStore::IsValidId(m_renameId, &why);
            if (!ok) { ImGui::TextColored(kColorError, "%s", why.c_str()); }
            ImGui::BeginDisabled(!ok || m_popupTarget == m_renameId);
            if (ImGui::Button("変更する"))
            {
                std::string message;
                const bool done = store.Rename(m_popupTarget, m_renameId, message);
                SetMessage(message, !done);
                if (done) { m_selected = m_renameId; }
                m_dirty = true;
                close = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("やめる")) { close = true; }
            break;
        }
        case Popup::Promote:
        {
            ImGui::TextWrapped("「%s」を、正式に採用します。TestMaps/ に置かれ、Git管理の対象になります。", target ? target->meta.name.c_str() : m_popupTarget.c_str());
            if (ImGui::Button("正式に採用する"))
            {
                std::string message;
                const bool ok = store.Move(m_popupTarget, TestMapFolder::Official, message);
                SetMessage(message, !ok);
                m_dirty = true;
                close = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("やめる")) { close = true; }
            break;
        }
        case Popup::Demote:
        {
            ImGui::TextWrapped("「%s」を、下書きへ戻します。TestMaps/Drafts/ に置かれ、Git管理の対象から、外れます。", target ? target->meta.name.c_str() : m_popupTarget.c_str());
            if (ImGui::Button("下書きへ戻す"))
            {
                std::string message;
                const bool ok = store.Move(m_popupTarget, TestMapFolder::Draft, message);
                SetMessage(message, !ok);
                m_dirty = true;
                close = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("やめる")) { close = true; }
            break;
        }
        case Popup::Delete:
        {
            ImGui::TextColored(kColorError, "「%s」(%s)を、削除します。元に戻せません。", target ? target->meta.name.c_str() : m_popupTarget.c_str(), m_popupTarget.c_str());
            if (target && target->folder == TestMapFolder::Official)
            {
                ImGui::TextWrapped("正式なテストマップです(Git管理のファイル)。削除すると、ファイルが消えます。");
            }
            ImGui::TextWrapped("確認のため、id「%s」を、打ち込んでください。", m_popupTarget.c_str());
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("##deleteConfirm", m_deleteConfirm, sizeof(m_deleteConfirm));
            ImGui::BeginDisabled(m_popupTarget != m_deleteConfirm);
            if (ImGui::Button("削除する"))
            {
                std::string message;
                const bool ok = store.Delete(m_popupTarget, message);
                SetMessage(message, !ok);
                if (ok && m_selected == m_popupTarget) { m_selected.clear(); }
                m_dirty = true;
                close = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("やめる")) { close = true; }
            break;
        }
        default:
            close = true;
            break;
        }
        if (close)
        {
            ImGui::CloseCurrentPopup();
            m_popup = Popup::None;
        }
        ImGui::EndPopup();
    }
    else if (!m_openPopupRequest)
    {
        m_popup = Popup::None;     // 外から閉じられた
    }
}
