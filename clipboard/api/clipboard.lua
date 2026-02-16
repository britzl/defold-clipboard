--[[
Clipboard API documentation.
Defold native extension to access the clipboard
--]]

---@class defold_api.clipboard
clipboard = {}


---Copy text to the clipboard.
---@param text string
function clipboard.copy(text) end


---Get text from the clipboard.
---@return string text
function clipboard.paste() end
