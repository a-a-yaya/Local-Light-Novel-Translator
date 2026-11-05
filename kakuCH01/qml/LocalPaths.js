.pragma library

// Preserve the translation file pickers' existing URL handling.
function filePath(url) {
    if (!url) return ""
    var value = String(url)
    return value.indexOf("file:///") === 0 ? value.substring(8) : value
}

function folderUrl(path) {
    if (!path) return "file:///"
    var value = String(path).replace(/\\/g, "/")
    return value.indexOf("file:///") === 0 ? value : "file:///" + encodeURI(value)
}
