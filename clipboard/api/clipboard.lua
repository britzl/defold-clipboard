---@meta
---@diagnostic disable: lowercase-global
---@diagnostic disable: missing-return
--[[
Defold native extension to access the clipboard
This is a generated file. Do not modify. Created using https://github.com/britzl/generatedocs
--]]

---@class defold_api.clipboard
clipboard = {}
---@param text string
function clipboard.copy(text) end
---@return string text
function clipboard.paste() end
