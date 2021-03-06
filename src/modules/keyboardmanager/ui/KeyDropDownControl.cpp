#include "pch.h"
#include "KeyDropDownControl.h"
#include "keyboardmanager/common/Helpers.h"
#include <keyboardmanager/common/KeyboardManagerState.h>

// Initialized to null
KeyboardManagerState* KeyDropDownControl::keyboardManagerState = nullptr;

// Function to set properties apart from the SelectionChanged event handler
void KeyDropDownControl::SetDefaultProperties(bool isShortcut)
{
    dropDown = ComboBox();
    warningFlyout = Flyout();
    warningMessage = TextBlock();

    if (!isShortcut)
    {
        dropDown.as<ComboBox>().Width(KeyboardManagerConstants::RemapTableDropDownWidth);
    }
    else
    {
        dropDown.as<ComboBox>().Width(KeyboardManagerConstants::ShortcutTableDropDownWidth);
    }
    dropDown.as<ComboBox>().MaxDropDownHeight(KeyboardManagerConstants::TableDropDownHeight);
    // Initialise layout attribute
    previousLayout = GetKeyboardLayout(0);
    keyCodeList = keyboardManagerState->keyboardMap.GetKeyCodeList(isShortcut);
    dropDown.as<ComboBox>().ItemsSource(KeyboardManagerHelper::ToBoxValue(keyboardManagerState->keyboardMap.GetKeyNameList(isShortcut)));
    // drop down open handler - to reload the items with the latest layout
    dropDown.as<ComboBox>().DropDownOpened([&, isShortcut](winrt::Windows::Foundation::IInspectable const& sender, auto args) {
        ComboBox currentDropDown = sender.as<ComboBox>();
        CheckAndUpdateKeyboardLayout(currentDropDown, isShortcut);
    });

    // Attach flyout to the drop down
    warningFlyout.as<Flyout>().Content(warningMessage.as<TextBlock>());
    dropDown.as<ComboBox>().ContextFlyout().SetAttachedFlyout((FrameworkElement)dropDown.as<ComboBox>(), warningFlyout.as<Flyout>());
}

// Function to check if the layout has changed and accordingly update the drop down list
void KeyDropDownControl::CheckAndUpdateKeyboardLayout(ComboBox currentDropDown, bool isShortcut)
{
    // Get keyboard layout for current thread
    HKL layout = GetKeyboardLayout(0);

    // Check if the layout has changed
    if (previousLayout != layout)
    {
        keyCodeList = keyboardManagerState->keyboardMap.GetKeyCodeList(isShortcut);
        currentDropDown.ItemsSource(KeyboardManagerHelper::ToBoxValue(keyboardManagerState->keyboardMap.GetKeyNameList(isShortcut)));
        previousLayout = layout;
    }
}

// Function to set selection handler for single key remap drop down. Needs to be called after the constructor since the singleKeyControl StackPanel is null if called in the constructor
void KeyDropDownControl::SetSelectionHandler(Grid& table, StackPanel singleKeyControl, int colIndex, std::vector<std::pair<std::vector<std::variant<DWORD, Shortcut>>, std::wstring>>& singleKeyRemapBuffer)
{
    // drop down selection handler
    auto onSelectionChange = [&, table, singleKeyControl, colIndex](winrt::Windows::Foundation::IInspectable const& sender) {
        ComboBox currentDropDown = sender.as<ComboBox>();
        int selectedKeyIndex = currentDropDown.SelectedIndex();
        // Get row index of the single key control
        uint32_t controlIndex;
        bool indexFound = table.Children().IndexOf(singleKeyControl, controlIndex);
        if (indexFound)
        {
            KeyboardManagerHelper::ErrorType errorType = KeyboardManagerHelper::ErrorType::NoError;
            int rowIndex = (controlIndex - KeyboardManagerConstants::RemapTableHeaderCount) / KeyboardManagerConstants::RemapTableColCount;
            // Check if the element was not found or the index exceeds the known keys
            if (selectedKeyIndex != -1 && keyCodeList.size() > selectedKeyIndex)
            {
                // Check if the value being set is the same as the other column
                if (singleKeyRemapBuffer[rowIndex].first[std::abs(int(colIndex) - 1)].index() == 0)
                {
                    if (std::get<DWORD>(singleKeyRemapBuffer[rowIndex].first[std::abs(int(colIndex) - 1)]) == keyCodeList[selectedKeyIndex])
                    {
                        errorType = KeyboardManagerHelper::ErrorType::MapToSameKey;
                    }
                }

                // If one column is shortcut and other is key no warning required

                if (errorType == KeyboardManagerHelper::ErrorType::NoError && colIndex == 0)
                {
                    // Check if the key is already remapped to something else
                    for (int i = 0; i < singleKeyRemapBuffer.size(); i++)
                    {
                        if (i != rowIndex)
                        {
                            if (singleKeyRemapBuffer[i].first[colIndex].index() == 0)
                            {
                                KeyboardManagerHelper::ErrorType result = KeyboardManagerHelper::DoKeysOverlap(std::get<DWORD>(singleKeyRemapBuffer[i].first[colIndex]), keyCodeList[selectedKeyIndex]);
                                if (result != KeyboardManagerHelper::ErrorType::NoError)
                                {
                                    errorType = result;
                                    break;
                                }
                            }
                            else
                            {
                                // check key to shortcut error
                            }
                        }
                    }
                }

                // If there is no error, set the buffer
                if (errorType == KeyboardManagerHelper::ErrorType::NoError)
                {
                    singleKeyRemapBuffer[rowIndex].first[colIndex] = keyCodeList[selectedKeyIndex];
                }
                else
                {
                    singleKeyRemapBuffer[rowIndex].first[colIndex] = NULL;
                }
            }
            else
            {
                // Reset to null if the key is not found
                singleKeyRemapBuffer[rowIndex].first[colIndex] = NULL;
            }

            if (errorType != KeyboardManagerHelper::ErrorType::NoError)
            {
                SetDropDownError(currentDropDown, KeyboardManagerHelper::GetErrorMessage(errorType));
            }
        }
    };

    // Rather than on every selection change (which gets triggered on searching as well) we set the handler only when the drop down is closed
    dropDown.as<ComboBox>().DropDownClosed([onSelectionChange](winrt::Windows::Foundation::IInspectable const& sender, auto const& args) {
        onSelectionChange(sender);
    });

    // We check if the selection changed was triggered while the drop down was closed. This is required to handle Type key, initial loading of remaps and if the user just types in the combo box without opening it
    dropDown.as<ComboBox>().SelectionChanged([onSelectionChange](winrt::Windows::Foundation::IInspectable const& sender, SelectionChangedEventArgs const& args) {
        ComboBox currentDropDown = sender.as<ComboBox>();
        if (!currentDropDown.IsDropDownOpen())
        {
            onSelectionChange(sender);
        }
    });
}

std::pair<KeyboardManagerHelper::ErrorType, int> KeyDropDownControl::ValidateShortcutSelection(Grid table, StackPanel shortcutControl, StackPanel parent, int colIndex, std::vector<std::pair<std::vector<std::variant<DWORD, Shortcut>>, std::wstring>>& shortcutRemapBuffer, std::vector<std::unique_ptr<KeyDropDownControl>>& keyDropDownControlObjects, TextBox targetApp, bool isHybridControl, bool isSingleKeyWindow)
{
    ComboBox currentDropDown = dropDown.as<ComboBox>();
    int selectedKeyIndex = currentDropDown.SelectedIndex();
    uint32_t dropDownIndex = -1;
    bool dropDownFound = parent.Children().IndexOf(currentDropDown, dropDownIndex);
    // Get row index of the single key control
    uint32_t controlIndex;
    bool controlIindexFound = table.Children().IndexOf(shortcutControl, controlIndex);
    KeyboardManagerHelper::ErrorType errorType = KeyboardManagerHelper::ErrorType::NoError;
    bool IsDeleteDropDownRequired = false;
    int rowIndex = -1;

    if (controlIindexFound)
    {
        if (isSingleKeyWindow)
        {
            rowIndex = (controlIndex - KeyboardManagerConstants::RemapTableHeaderCount) / KeyboardManagerConstants::RemapTableColCount;
        }
        else
        {
            rowIndex = (controlIndex - KeyboardManagerConstants::ShortcutTableHeaderCount) / KeyboardManagerConstants::ShortcutTableColCount;
        }
        if (selectedKeyIndex != -1 && keyCodeList.size() > selectedKeyIndex && dropDownFound)
        {
            // If only 1 drop down and action key is chosen: Warn that a modifier must be chosen (if the drop down is not for a hybrid scenario)
            if (parent.Children().Size() == 1 && !KeyboardManagerHelper::IsModifierKey(keyCodeList[selectedKeyIndex]) && !isHybridControl)
            {
                // warn and reset the drop down
                errorType = KeyboardManagerHelper::ErrorType::ShortcutStartWithModifier;
            }
            // If it is the last drop down
            else if (dropDownIndex == parent.Children().Size() - 1)
            {
                // If last drop down and a modifier is selected: add a new drop down (max drop down count should be enforced)
                if (KeyboardManagerHelper::IsModifierKey(keyCodeList[selectedKeyIndex]) && parent.Children().Size() < KeyboardManagerConstants::MaxShortcutSize)
                {
                    // If it matched any of the previous modifiers then reset that drop down
                    if (CheckRepeatedModifier(parent, selectedKeyIndex, keyCodeList))
                    {
                        // warn and reset the drop down
                        errorType = KeyboardManagerHelper::ErrorType::ShortcutCannotHaveRepeatedModifier;
                    }
                    // If not, add a new drop down
                    else
                    {
                        AddDropDown(table, shortcutControl, parent, colIndex, shortcutRemapBuffer, keyDropDownControlObjects, targetApp, isHybridControl, isSingleKeyWindow);
                    }
                }
                // If last drop down and a modifier is selected but there are already max drop downs: warn the user
                else if (KeyboardManagerHelper::IsModifierKey(keyCodeList[selectedKeyIndex]) && parent.Children().Size() >= KeyboardManagerConstants::MaxShortcutSize)
                {
                    // warn and reset the drop down
                    errorType = KeyboardManagerHelper::ErrorType::ShortcutMaxShortcutSizeOneActionKey;
                }
                // If None is selected but it's the last index: warn
                else if (keyCodeList[selectedKeyIndex] == 0)
                {
                    // If it is a hybrid control and there are 2 drop downs then deletion is allowed
                    if (isHybridControl && parent.Children().Size() == KeyboardManagerConstants::MinShortcutSize)
                    {
                        // set delete drop down flag
                        IsDeleteDropDownRequired = true;
                        // do not delete the drop down now since there may be some other error which would cause the drop down to be invalid after removal
                    }
                    else
                    {
                        // warn and reset the drop down
                        errorType = KeyboardManagerHelper::ErrorType::ShortcutOneActionKey;
                    }
                }
                // If none of the above, then the action key will be set
            }
            // If it is the not the last drop down
            else
            {
                if (KeyboardManagerHelper::IsModifierKey(keyCodeList[selectedKeyIndex]))
                {
                    // If it matched any of the previous modifiers then reset that drop down
                    if (CheckRepeatedModifier(parent, selectedKeyIndex, keyCodeList))
                    {
                        // warn and reset the drop down
                        errorType = KeyboardManagerHelper::ErrorType::ShortcutCannotHaveRepeatedModifier;
                    }
                    // If not, the modifier key will be set
                }
                // If None is selected and there are more than 2 drop downs
                else if (keyCodeList[selectedKeyIndex] == 0 && parent.Children().Size() > KeyboardManagerConstants::MinShortcutSize)
                {
                    // set delete drop down flag
                    IsDeleteDropDownRequired = true;
                    // do not delete the drop down now since there may be some other error which would cause the drop down to be invalid after removal
                }
                else if (keyCodeList[selectedKeyIndex] == 0 && parent.Children().Size() <= KeyboardManagerConstants::MinShortcutSize)
                {
                    // If it is a hybrid control and there are 2 drop downs then deletion is allowed
                    if (isHybridControl && parent.Children().Size() == KeyboardManagerConstants::MinShortcutSize)
                    {
                        // set delete drop down flag
                        IsDeleteDropDownRequired = true;
                        // do not delete the drop down now since there may be some other error which would cause the drop down to be invalid after removal
                    }
                    else
                    {
                        // warn and reset the drop down
                        errorType = KeyboardManagerHelper::ErrorType::ShortcutAtleast2Keys;
                    }
                }
                // If the user tries to set an action key check if all drop down menus after this are empty if it is not the first key. If it is a hybrid control, this can be done even on the first key
                else if (dropDownIndex != 0 || isHybridControl)
                {
                    bool isClear = true;
                    for (int i = dropDownIndex + 1; i < (int)parent.Children().Size(); i++)
                    {
                        ComboBox ItDropDown = parent.Children().GetAt(i).as<ComboBox>();
                        if (ItDropDown.SelectedIndex() != -1)
                        {
                            isClear = false;
                            break;
                        }
                    }

                    if (isClear)
                    {
                        // remove all the drop down
                        int elementsToBeRemoved = parent.Children().Size() - dropDownIndex - 1;
                        for (int i = 0; i < elementsToBeRemoved; i++)
                        {
                            parent.Children().RemoveAtEnd();
                            keyDropDownControlObjects.erase(keyDropDownControlObjects.end() - 1);
                        }
                        parent.UpdateLayout();
                    }
                    else
                    {
                        // warn and reset the drop down
                        errorType = KeyboardManagerHelper::ErrorType::ShortcutNotMoreThanOneActionKey;
                    }
                }
                // If there an action key is chosen on the first drop down and there are more than one drop down menus
                else
                {
                    // warn and reset the drop down
                    errorType = KeyboardManagerHelper::ErrorType::ShortcutStartWithModifier;
                }
            }
        }

        // After validating the shortcut, now for errors like remap to same shortcut, remap shortcut more than once, Win L and Ctrl Alt Del
        if (errorType == KeyboardManagerHelper::ErrorType::NoError)
        {
            std::variant<DWORD, Shortcut> tempShortcut;
            std::vector<DWORD> selectedKeyCodes = GetKeysFromStackPanel(parent);
            if (isHybridControl && selectedKeyCodes.size() == 1)
            {
                tempShortcut = selectedKeyCodes[0];
            }
            else
            {
                tempShortcut = Shortcut();
                std::get<Shortcut>(tempShortcut).SetKeyCodes(GetKeysFromStackPanel(parent));
            }
            std::wstring appName;
            if (targetApp != nullptr)
            {
                appName = targetApp.Text().c_str();
            }
            // Convert app name to lower case
            std::transform(appName.begin(), appName.end(), appName.begin(), towlower);
            std::wstring lowercaseDefAppName = KeyboardManagerConstants::DefaultAppName;
            std::transform(lowercaseDefAppName.begin(), lowercaseDefAppName.end(), lowercaseDefAppName.begin(), towlower);
            if (appName == lowercaseDefAppName)
            {
                appName = L"";
            }

            // Check if the value being set is the same as the other column - index of other column does not have to be checked since only one column is hybrid
            if (tempShortcut.index() == 1)
            {
                // If shortcut to shortcut
                if (shortcutRemapBuffer[rowIndex].first[std::abs(int(colIndex) - 1)].index() == 1)
                {
                    if (std::get<Shortcut>(shortcutRemapBuffer[rowIndex].first[std::abs(int(colIndex) - 1)]) == std::get<Shortcut>(tempShortcut) && std::get<Shortcut>(shortcutRemapBuffer[rowIndex].first[std::abs(int(colIndex) - 1)]).IsValidShortcut() && std::get<Shortcut>(tempShortcut).IsValidShortcut())
                    {
                        errorType = KeyboardManagerHelper::ErrorType::MapToSameShortcut;
                    }
                }

                // If one column is shortcut and other is key no warning required
            }
            else
            {
                // If key to key
                if (shortcutRemapBuffer[rowIndex].first[std::abs(int(colIndex) - 1)].index() == 0)
                {
                    if (std::get<DWORD>(shortcutRemapBuffer[rowIndex].first[std::abs(int(colIndex) - 1)]) == std::get<DWORD>(tempShortcut) && std::get<DWORD>(shortcutRemapBuffer[rowIndex].first[std::abs(int(colIndex) - 1)]) != NULL && std::get<DWORD>(tempShortcut) != NULL)
                    {
                        errorType = KeyboardManagerHelper::ErrorType::MapToSameKey;
                    }
                }

                // If one column is shortcut and other is key no warning required
            }

            if (errorType == KeyboardManagerHelper::ErrorType::NoError && colIndex == 0)
            {
                // Check if the key is already remapped to something else for the same target app
                for (int i = 0; i < shortcutRemapBuffer.size(); i++)
                {
                    std::wstring currAppName = shortcutRemapBuffer[i].second;
                    std::transform(currAppName.begin(), currAppName.end(), currAppName.begin(), towlower);

                    if (i != rowIndex && currAppName == appName)
                    {
                        KeyboardManagerHelper::ErrorType result = KeyboardManagerHelper::ErrorType::NoError;
                        if (!isHybridControl)
                        {
                            result = Shortcut::DoKeysOverlap(std::get<Shortcut>(shortcutRemapBuffer[i].first[colIndex]), std::get<Shortcut>(tempShortcut));
                        }
                        else
                        {
                            if (tempShortcut.index() == 0 && shortcutRemapBuffer[i].first[colIndex].index() == 0)
                            {
                                if (std::get<DWORD>(tempShortcut) != NULL && std::get<DWORD>(shortcutRemapBuffer[i].first[colIndex]) != NULL)
                                {
                                    result = KeyboardManagerHelper::DoKeysOverlap(std::get<DWORD>(shortcutRemapBuffer[i].first[colIndex]), std::get<DWORD>(tempShortcut));
                                }
                            }
                            else if (tempShortcut.index() == 1 && shortcutRemapBuffer[i].first[colIndex].index() == 1)
                            {
                                if (std::get<Shortcut>(tempShortcut).IsValidShortcut() && std::get<Shortcut>(shortcutRemapBuffer[i].first[colIndex]).IsValidShortcut())
                                {
                                    result = Shortcut::DoKeysOverlap(std::get<Shortcut>(shortcutRemapBuffer[i].first[colIndex]), std::get<Shortcut>(tempShortcut));
                                }
                            }
                            // Other scenarios not possible since key to shortcut is with key to key, and shortcut to key is with shortcut to shortcut
                        }
                        if (result != KeyboardManagerHelper::ErrorType::NoError)
                        {
                            errorType = result;
                            break;
                        }
                    }
                }
            }

            if (errorType == KeyboardManagerHelper::ErrorType::NoError && tempShortcut.index() == 1)
            {
                errorType = std::get<Shortcut>(tempShortcut).IsShortcutIllegal();
            }
        }

        if (errorType != KeyboardManagerHelper::ErrorType::NoError)
        {
            SetDropDownError(currentDropDown, KeyboardManagerHelper::GetErrorMessage(errorType));
        }

        // Handle None case if there are no other errors
        else if (IsDeleteDropDownRequired)
        {
            parent.Children().RemoveAt(dropDownIndex);
            // delete drop down control object from the vector so that it can be destructed
            keyDropDownControlObjects.erase(keyDropDownControlObjects.begin() + dropDownIndex);
            parent.UpdateLayout();
        }
    }

    return std::make_pair(errorType, rowIndex);
}

// Function to set selection handler for shortcut drop down. Needs to be called after the constructor since the shortcutControl StackPanel is null if called in the constructor
void KeyDropDownControl::SetSelectionHandler(Grid& table, StackPanel shortcutControl, StackPanel parent, int colIndex, std::vector<std::pair<std::vector<std::variant<DWORD, Shortcut>>, std::wstring>>& shortcutRemapBuffer, std::vector<std::unique_ptr<KeyDropDownControl>>& keyDropDownControlObjects, TextBox& targetApp, bool isHybridControl, bool isSingleKeyWindow)
{
    auto onSelectionChange = [&, table, shortcutControl, colIndex, parent, targetApp, isHybridControl, isSingleKeyWindow](winrt::Windows::Foundation::IInspectable const& sender) {
        std::pair<KeyboardManagerHelper::ErrorType, int> validationResult = ValidateShortcutSelection(table, shortcutControl, parent, colIndex, shortcutRemapBuffer, keyDropDownControlObjects, targetApp, isHybridControl, isSingleKeyWindow);

        // Check if the drop down row index was identified from the return value of validateSelection
        if (validationResult.second != -1)
        {
            // If an error occurred
            if (validationResult.first != KeyboardManagerHelper::ErrorType::NoError)
            {
                // Validate all the drop downs
                ValidateShortcutFromDropDownList(table, shortcutControl, parent, colIndex, shortcutRemapBuffer, keyDropDownControlObjects, targetApp, isHybridControl, isSingleKeyWindow);
            }

            // Reset the buffer based on the new selected drop down items
            std::vector selectedKeyCodes = GetKeysFromStackPanel(parent);
            if (!isHybridControl)
            {
                std::get<Shortcut>(shortcutRemapBuffer[validationResult.second].first[colIndex]).SetKeyCodes(selectedKeyCodes);
            }
            else
            {
                // If exactly one key is selected consider it to be a key remap
                if (selectedKeyCodes.size() == 1)
                {
                    shortcutRemapBuffer[validationResult.second].first[colIndex] = selectedKeyCodes[0];
                }
                else
                {
                    Shortcut tempShortcut;
                    tempShortcut.SetKeyCodes(selectedKeyCodes);
                    // Assign instead of setting the value in the buffer since the previous value may not be a Shortcut
                    shortcutRemapBuffer[validationResult.second].first[colIndex] = tempShortcut;
                }
            }
            if (targetApp != nullptr)
            {
                std::wstring newText = targetApp.Text().c_str();
                std::wstring lowercaseDefAppName = KeyboardManagerConstants::DefaultAppName;
                std::transform(newText.begin(), newText.end(), newText.begin(), towlower);
                std::transform(lowercaseDefAppName.begin(), lowercaseDefAppName.end(), lowercaseDefAppName.begin(), towlower);
                if (newText == lowercaseDefAppName)
                {
                    shortcutRemapBuffer[validationResult.second].second = L"";
                }
                else
                {
                    shortcutRemapBuffer[validationResult.second].second = targetApp.Text().c_str();
                }
            }
        }

        // If the user searches for a key the selection handler gets invoked however if they click away it reverts back to the previous state. This can result in dangling references to added drop downs which were then reset.
        // We handle this by removing the drop down if it no longer a child of the parent
        for (long long i = keyDropDownControlObjects.size() - 1; i >= 0; i--)
        {
            uint32_t index;
            bool found = parent.Children().IndexOf(keyDropDownControlObjects[i]->GetComboBox(), index);
            if (!found)
            {
                keyDropDownControlObjects.erase(keyDropDownControlObjects.begin() + i);
            }
        }
    };

    // Rather than on every selection change (which gets triggered on searching as well) we set the handler only when the drop down is closed
    dropDown.as<ComboBox>().DropDownClosed([onSelectionChange](winrt::Windows::Foundation::IInspectable const& sender, auto const& args) {
        onSelectionChange(sender);
    });

    // We check if the selection changed was triggered while the drop down was closed. This is required to handle Type key, initial loading of remaps and if the user just types in the combo box without opening it
    dropDown.as<ComboBox>().SelectionChanged([onSelectionChange](winrt::Windows::Foundation::IInspectable const& sender, SelectionChangedEventArgs const& args) {
        ComboBox currentDropDown = sender.as<ComboBox>();
        if (!currentDropDown.IsDropDownOpen())
        {
            onSelectionChange(sender);
        }
    });
}

// Function to set the selected index of the drop down
void KeyDropDownControl::SetSelectedIndex(int32_t index)
{
    dropDown.as<ComboBox>().SelectedIndex(index);
}

// Function to return the combo box element of the drop down
ComboBox KeyDropDownControl::GetComboBox()
{
    return dropDown.as<ComboBox>();
}

// Function to add a drop down to the shortcut stack panel
void KeyDropDownControl::AddDropDown(Grid table, StackPanel shortcutControl, StackPanel parent, const int colIndex, std::vector<std::pair<std::vector<std::variant<DWORD, Shortcut>>, std::wstring>>& shortcutRemapBuffer, std::vector<std::unique_ptr<KeyDropDownControl>>& keyDropDownControlObjects, TextBox targetApp, bool isHybridControl, bool isSingleKeyWindow)
{
    keyDropDownControlObjects.push_back(std::move(std::unique_ptr<KeyDropDownControl>(new KeyDropDownControl(true))));
    parent.Children().Append(keyDropDownControlObjects[keyDropDownControlObjects.size() - 1]->GetComboBox());
    keyDropDownControlObjects[keyDropDownControlObjects.size() - 1]->SetSelectionHandler(table, shortcutControl, parent, colIndex, shortcutRemapBuffer, keyDropDownControlObjects, targetApp, isHybridControl, isSingleKeyWindow);
    parent.UpdateLayout();
}

// Function to get the list of key codes from the shortcut combo box stack panel
std::vector<DWORD> KeyDropDownControl::GetKeysFromStackPanel(StackPanel parent)
{
    std::vector<DWORD> keys;
    std::vector<DWORD> keyCodeList = keyboardManagerState->keyboardMap.GetKeyCodeList(true);
    for (int i = 0; i < (int)parent.Children().Size(); i++)
    {
        ComboBox currentDropDown = parent.Children().GetAt(i).as<ComboBox>();
        int selectedKeyIndex = currentDropDown.SelectedIndex();
        if (selectedKeyIndex != -1 && keyCodeList.size() > selectedKeyIndex)
        {
            // If None is not the selected key
            if (keyCodeList[selectedKeyIndex] != 0)
            {
                keys.push_back(keyCodeList[selectedKeyIndex]);
            }
        }
    }

    return keys;
}

// Function to check if a modifier has been repeated in the previous drop downs
bool KeyDropDownControl::CheckRepeatedModifier(StackPanel parent, int selectedKeyIndex, const std::vector<DWORD>& keyCodeList)
{
    // check if modifier has already been added before in a previous drop down
    std::vector<DWORD> currentKeys = GetKeysFromStackPanel(parent);
    int currentDropDownIndex = -1;

    // Find the key index of the current drop down selection so that we skip that index while searching for repeated modifiers
    for (int i = 0; i < currentKeys.size(); i++)
    {
        if (currentKeys[i] == keyCodeList[selectedKeyIndex])
        {
            currentDropDownIndex = i;
            break;
        }
    }

    bool matchPreviousModifier = false;
    for (int i = 0; i < currentKeys.size(); i++)
    {
        // Skip the current drop down
        if (i != currentDropDownIndex)
        {
            // If the key type for the newly added key matches any of the existing keys in the shortcut
            if (KeyboardManagerHelper::GetKeyType(keyCodeList[selectedKeyIndex]) == KeyboardManagerHelper::GetKeyType(currentKeys[i]))
            {
                matchPreviousModifier = true;
                break;
            }
        }
    }

    return matchPreviousModifier;
}

// Function for validating the selection of shortcuts for all the associated drop downs
void KeyDropDownControl::ValidateShortcutFromDropDownList(Grid table, StackPanel shortcutControl, StackPanel parent, int colIndex, std::vector<std::pair<std::vector<std::variant<DWORD, Shortcut>>, std::wstring>>& shortcutRemapBuffer, std::vector<std::unique_ptr<KeyDropDownControl>>& keyDropDownControlObjects, TextBox targetApp, bool isHybridControl, bool isSingleKeyWindow)
{
    // Iterate over all drop downs from left to right in that row/col and validate if there is an error in any of the drop downs. After this the state should be error-free (if it is a valid shortcut)
    for (int i = 0; i < keyDropDownControlObjects.size(); i++)
    {
        // Check for errors only if the current selection is a valid shortcut
        std::vector<DWORD> selectedKeyCodes = keyDropDownControlObjects[i]->GetKeysFromStackPanel(parent);
        std::variant<DWORD, Shortcut> currentShortcut;
        if (selectedKeyCodes.size() == 1 && isHybridControl)
        {
            currentShortcut = selectedKeyCodes[0];
        }
        else
        {
            Shortcut temp;
            temp.SetKeyCodes(selectedKeyCodes);
            currentShortcut = temp;
        }

        // If the key/shortcut is valid and that drop down is not empty
        if (((currentShortcut.index() == 0 && std::get<DWORD>(currentShortcut) != NULL) || (currentShortcut.index() == 1 && std::get<Shortcut>(currentShortcut).IsValidShortcut())) && keyDropDownControlObjects[i]->GetComboBox().SelectedIndex() != -1)
        {
            keyDropDownControlObjects[i]->ValidateShortcutSelection(table, shortcutControl, parent, colIndex, shortcutRemapBuffer, keyDropDownControlObjects, targetApp, isHybridControl, isSingleKeyWindow);
        }
    }
}

// Function to set the warning message
void KeyDropDownControl::SetDropDownError(ComboBox currentDropDown, hstring message)
{
    currentDropDown.SelectedIndex(-1);
    warningMessage.as<TextBlock>().Text(message);
    currentDropDown.ContextFlyout().ShowAttachedFlyout((FrameworkElement)dropDown.as<ComboBox>());
}

// Function to add a shortcut to the UI control as combo boxes
void KeyDropDownControl::AddShortcutToControl(Shortcut shortcut, Grid table, StackPanel parent, KeyboardManagerState& keyboardManagerState, const int colIndex, std::vector<std::unique_ptr<KeyDropDownControl>>& keyDropDownControlObjects, std::vector<std::pair<std::vector<std::variant<DWORD, Shortcut>>, std::wstring>>& remapBuffer, StackPanel controlLayout, TextBox targetApp, bool isHybridControl, bool isSingleKeyWindow)
{
    // Delete the existing drop down menus
    parent.Children().Clear();
    // Remove references to the old drop down objects to destroy them
    keyDropDownControlObjects.clear();

    std::vector<DWORD> shortcutKeyCodes = shortcut.GetKeyCodes();
    std::vector<DWORD> keyCodeList = keyboardManagerState.keyboardMap.GetKeyCodeList(true);
    if (shortcutKeyCodes.size() != 0)
    {
        KeyDropDownControl::AddDropDown(table, controlLayout, parent, colIndex, remapBuffer, keyDropDownControlObjects, targetApp, isHybridControl, isSingleKeyWindow);
        for (int i = 0; i < shortcutKeyCodes.size(); i++)
        {
            // New drop down gets added automatically when the SelectedIndex is set
            if (i < (int)parent.Children().Size())
            {
                ComboBox currentDropDown = parent.Children().GetAt(i).as<ComboBox>();
                auto it = std::find(keyCodeList.begin(), keyCodeList.end(), shortcutKeyCodes[i]);
                if (it != keyCodeList.end())
                {
                    currentDropDown.SelectedIndex((int32_t)std::distance(keyCodeList.begin(), it));
                }
            }
        }
    }
    parent.UpdateLayout();
}
