#include "recoder.h"

#include <windows.h>
#include <appmodel.h>
#include <shobjidl.h>
#include <microsoft.ui.xaml.window.h>
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

extern "C" {
HRESULT __stdcall MddBootstrapInitialize2(UINT32, PCWSTR, PACKAGE_VERSION, UINT32) noexcept;
void __stdcall MddBootstrapShutdown() noexcept;
}

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::ApplicationModel::DataTransfer;

namespace {
constexpr std::uintmax_t max_file_size = 16 * 1024 * 1024;
constexpr const wchar_t* encodings[] = {
    L"UTF-8", L"UTF-16LE", L"UTF-16BE", L"UTF-32LE", L"UTF-32BE",
    L"GB18030", L"GBK", L"BIG5", L"SHIFT_JIS", L"EUC-JP",
    L"EUC-KR", L"WINDOWS-1252", L"ISO-8859-1", L"ASCII"
};

std::wstring choose_file(HWND owner, bool save) {
    com_ptr<IFileDialog> dialog;
    if (save) {
        com_ptr<IFileSaveDialog> instance;
        check_hresult(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(instance.put())));
        dialog = instance.as<IFileDialog>();
        check_hresult(instance->SetFileName(L"converted.txt"));
    } else {
        com_ptr<IFileOpenDialog> instance;
        check_hresult(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(instance.put())));
        dialog = instance.as<IFileDialog>();
    }
    DWORD options{};
    check_hresult(dialog->GetOptions(&options));
    check_hresult(dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR |
                                      (save ? FOS_PATHMUSTEXIST : FOS_FILEMUSTEXIST)));
    HRESULT result = dialog->Show(owner);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {};
    check_hresult(result);
    com_ptr<IShellItem> item;
    check_hresult(dialog->GetResult(item.put()));
    PWSTR path{};
    check_hresult(item->GetDisplayName(SIGDN_FILESYSPATH, &path));
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> guard(path, &CoTaskMemFree);
    return path;
}

std::string read_file(std::filesystem::path const& path) {
    auto size = std::filesystem::file_size(path);
    if (size > max_file_size) throw std::runtime_error("文件超过 16 MiB 限制");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("无法打开文件");
    std::string bytes(static_cast<size_t>(size), '\0');
    if (!bytes.empty() && !input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("文件读取失败");
    if (input.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("读取时文件已发生变化，请重试");
    if (input.bad()) throw std::runtime_error("文件读取失败");
    return bytes;
}

void save_new_file(std::wstring const& path, std::string const& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS)
            throw std::runtime_error("目标文件已存在，请选择新的文件名");
        throw_last_error();
    }
    size_t written = 0;
    DWORD error = ERROR_SUCCESS;
    while (written < bytes.size()) {
        DWORD chunk = static_cast<DWORD>((std::min)(bytes.size() - written, size_t{1024 * 1024}));
        DWORD count = 0;
        if (!WriteFile(file, bytes.data() + written, chunk, &count, nullptr)) {
            error = GetLastError();
            break;
        }
        if (count == 0) { error = ERROR_WRITE_FAULT; break; }
        written += count;
    }
    if (!CloseHandle(file) && error == ERROR_SUCCESS) error = GetLastError();
    if (error != ERROR_SUCCESS) {
        if (!DeleteFileW(path.c_str()))
            throw std::runtime_error("写入失败，且无法删除不完整的目标文件");
        throw hresult_error(HRESULT_FROM_WIN32(error));
    }
}
}

struct App : ApplicationT<App, Microsoft::UI::Xaml::Markup::IXamlMetadataProvider> {
    Microsoft::UI::Xaml::Markup::IXamlType GetXamlType(Windows::UI::Xaml::Interop::TypeName const& name) { return metadata().GetXamlType(name); }
    Microsoft::UI::Xaml::Markup::IXamlType GetXamlType(hstring const& name) { return metadata().GetXamlType(name); }
    com_array<Microsoft::UI::Xaml::Markup::XmlnsDefinition> GetXmlnsDefinitions() { return metadata().GetXmlnsDefinitions(); }

    void OnLaunched(LaunchActivatedEventArgs const&) {
        try {
            create_window();
        } catch (hresult_error const& ex) {
            MessageBoxW(nullptr, ex.message().c_str(), L"SmartRecoder", MB_ICONERROR);
            Exit();
        } catch (std::exception const& ex) {
            MessageBoxW(nullptr, to_hstring(ex.what()).c_str(), L"SmartRecoder", MB_ICONERROR);
            Exit();
        }
    }

private:
    Microsoft::UI::Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider metadata() {
        if (!metadata_provider) metadata_provider = Microsoft::UI::Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider();
        return metadata_provider;
    }
    void create_window() {
        Resources().MergedDictionaries().Append(XamlControlsResources());
        window = Window();
        window.Title(L"SmartRecoder - 文本编码转换");
        auto root = Grid();
        root.Padding(Thickness{16, 16, 16, 16});
        root.RowSpacing(10);
        auto top_row = RowDefinition();
        top_row.Height(GridLengthHelper::Auto());
        auto preview_row = RowDefinition();
        preview_row.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        root.RowDefinitions().Append(top_row);
        root.RowDefinitions().Append(preview_row);

        auto panel = StackPanel();
        panel.Spacing(8);
        auto open = Button();
        open.Content(box_value(L"打开文件"));
        open.Click([this](auto&&, auto&&) {
            try {
                auto path = choose_file(hwnd(), false);
                if (!path.empty()) load(path);
            } catch (hresult_error const& ex) { status.Text(ex.message()); }
              catch (std::exception const& ex) { status.Text(to_hstring(ex.what())); }
        });
        panel.Children().Append(open);
        file_label = TextBlock();
        file_label.Text(L"打开或拖放一个文本文件（最大 16 MiB）");
        file_label.TextWrapping(TextWrapping::Wrap);
        panel.Children().Append(file_label);

        auto source_row = StackPanel();
        source_row.Orientation(Orientation::Horizontal);
        source_row.Spacing(8);
        auto source_label = TextBlock();
        source_label.Text(L"源编码：");
        source_label.VerticalAlignment(VerticalAlignment::Center);
        source_row.Children().Append(source_label);
        source = TextBox();
        source.Width(180);
        source.PlaceholderText(L"选择或输入编码");
        source.TextChanged([this](auto&&, auto&&) { if (!loading) update_preview(); });
        source_row.Children().Append(source);
        auto source_list = make_encoding_list();
        source_list.SelectionChanged([this](auto const& sender, auto&&) {
            auto selected = sender.as<ComboBox>().SelectedItem();
            if (selected) source.Text(unbox_value<hstring>(selected));
        });
        source_row.Children().Append(source_list);
        panel.Children().Append(source_row);

        auto target_row = StackPanel();
        target_row.Orientation(Orientation::Horizontal);
        target_row.Spacing(8);
        auto target_label = TextBlock();
        target_label.Text(L"目标编码：");
        target_label.VerticalAlignment(VerticalAlignment::Center);
        target_row.Children().Append(target_label);
        target = TextBox();
        target.Width(180);
        target.Text(L"UTF-8");
        target_row.Children().Append(target);
        auto target_list = make_encoding_list();
        target_list.SelectionChanged([this](auto const& sender, auto&&) {
            auto selected = sender.as<ComboBox>().SelectedItem();
            if (selected) target.Text(unbox_value<hstring>(selected));
        });
        target_row.Children().Append(target_list);
        with_bom = CheckBox();
        with_bom.Content(box_value(L"添加 BOM"));
        target_row.Children().Append(with_bom);
        save = Button();
        save.Content(box_value(L"转换并另存为"));
        save.IsEnabled(false);
        save.Click([this](auto&&, auto&&) {
            try {
                auto output = recoder::encode(decoded, to_string(target.Text()), with_bom.IsChecked().GetBoolean());
                auto path = choose_file(hwnd(), true);
                if (path.empty()) return;
                save_new_file(path, output);
                status.Text(L"转换完成，已保存新文件。");
            } catch (hresult_error const& ex) { status.Text(ex.message()); }
              catch (std::exception const& ex) { status.Text(to_hstring(ex.what())); }
        });
        target_row.Children().Append(save);
        panel.Children().Append(target_row);
        status = TextBlock();
        status.TextWrapping(TextWrapping::Wrap);
        panel.Children().Append(status);
        root.Children().Append(panel);

        preview = TextBox();
        preview.IsReadOnly(true);
        preview.AcceptsReturn(true);
        preview.TextWrapping(TextWrapping::Wrap);
        preview.PlaceholderText(L"文本预览");
        Grid::SetRow(preview, 1);
        root.Children().Append(preview);
        root.AllowDrop(true);
        root.DragOver([](auto&&, DragEventArgs const& args) {
            if (args.DataView().Contains(StandardDataFormats::StorageItems()))
                args.AcceptedOperation(DataPackageOperation::Copy);
        });
        root.Drop([this](auto&&, DragEventArgs const& args) { handle_drop(args); });
        window.Content(root);
        window.Activate();
    }

    HWND hwnd() const {
        HWND result{};
        check_hresult(window.as<IWindowNative>()->get_WindowHandle(&result));
        return result;
    }
    ComboBox make_encoding_list() {
        ComboBox list;
        list.PlaceholderText(L"常用编码");
        for (auto encoding : encodings) list.Items().Append(box_value(encoding));
        return list;
    }
    void load(std::wstring const& path) {
        auto new_bytes = read_file(path);
        auto detected = recoder::detect(new_bytes);
        bytes = std::move(new_bytes);
        loaded = true;
        file_label.Text(hstring(L"文件：") + hstring(path) + L"　检测结果：" +
                        (detected.empty() ? hstring(L"无法确定") : to_hstring(detected)));
        loading = true;
        source.Text(to_hstring(detected));
        loading = false;
        update_preview();
    }
    void update_preview() {
        save.IsEnabled(false);
        decoded.clear();
        preview.Text(L"");
        if (!loaded) return;
        try {
            auto encoding = to_string(source.Text());
            if (encoding.empty()) { status.Text(L"请选择或输入源编码。"); return; }
            auto text = recoder::decode(bytes, encoding);
            if (text.find('\0') != std::string::npos)
                throw std::runtime_error("文件包含 NUL 字符，无法作为文本预览");
            preview.Text(to_hstring(text));
            decoded = std::move(text);
            save.IsEnabled(true);
            status.Text(L"请检查预览；如有乱码，请手动更换源编码。");
        } catch (hresult_error const& ex) { status.Text(ex.message()); }
          catch (std::exception const& ex) { status.Text(to_hstring(ex.what())); }
    }
    fire_and_forget handle_drop(DragEventArgs args) {
        auto lifetime = get_strong();
        auto deferral = args.GetDeferral();
        try {
            auto items = co_await args.DataView().GetStorageItemsAsync();
            if (items.Size() != 1) throw std::runtime_error("请拖放一个文件");
            auto file = items.GetAt(0).try_as<Windows::Storage::StorageFile>();
            if (!file || file.Path().empty()) throw std::runtime_error("请拖放本地文件");
            load(file.Path().c_str());
        } catch (hresult_error const& ex) { status.Text(ex.message()); }
          catch (std::exception const& ex) { status.Text(to_hstring(ex.what())); }
        deferral.Complete();
    }

    Window window{nullptr};
    Microsoft::UI::Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider metadata_provider{nullptr};
    TextBlock file_label{nullptr}, status{nullptr};
    TextBox source{nullptr}, target{nullptr}, preview{nullptr};
    CheckBox with_bom{nullptr};
    Button save{nullptr};
    std::string bytes, decoded;
    bool loaded = false, loading = false;
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        init_apartment(apartment_type::single_threaded);
        PACKAGE_VERSION min_version{};
        min_version.Version = 0x1F4002A304760000u;
        check_hresult(MddBootstrapInitialize2(0x00010008, L"", min_version, 0x0010));
        Application::Start([](auto&&) { make<App>(); });
        MddBootstrapShutdown();
    } catch (hresult_error const& ex) {
        MessageBoxW(nullptr, ex.message().c_str(), L"SmartRecoder", MB_ICONERROR);
        return static_cast<int>(ex.code());
    } catch (std::exception const& ex) {
        MessageBoxW(nullptr, to_hstring(ex.what()).c_str(), L"SmartRecoder", MB_ICONERROR);
        return 1;
    }
    return 0;
}
