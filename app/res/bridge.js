/*
 * SG Office -- window.AscDesktopEditor: what the editors (ONLYOFFICE's sdkjs
 * and web-apps, built for desktop) call on the program around them, here
 * answered by SG Office through sgoffice://app/native/.
 *
 * Calls the editors make synchronously are answered with synchronous
 * requests; the rest with fetch. Every call back into the editors
 * (DesktopOfflineAppDocumentEndLoad, ...EndSave) is made from here, when the
 * program's answer arrives. Features SG Office does not offer (encryption,
 * signatures, plugins, media, cloud printing) answer "not supported", which
 * the editors check for.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
(function () {
	"use strict";
	if (location.protocol !== "sgoffice:" || window.AscDesktopEditor)
		return;

	var docId = "";
	try { docId = new URL(window.top.location.href).searchParams.get("doc") || ""; } catch (e) {}
	if (!docId)
		return;

	var base = "sgoffice://app/native/";
	function url(call, query) {
		var u = base + call + "?doc=" + encodeURIComponent(docId);
		if (query)
			for (var k in query)
				u += "&" + k + "=" + encodeURIComponent(query[k]);
		return u;
	}
	function callSync(call, query, body) {
		var x = new XMLHttpRequest();
		x.open(body === undefined ? "GET" : "POST", url(call, query), false);
		x.send(body === undefined ? null : body);
		return x.status === 200 ? JSON.parse(x.responseText) : null;
	}
	function callAsync(call, query, body) {
		return fetch(url(call, query), body === undefined ? {} : { method: "POST", body: body })
			.then(function (r) { if (!r.ok) throw new Error(call + ": " + r.status); return r.json(); });
	}
	function log(msg) {
		try { callAsync("log", null, String(msg)); } catch (e) {}
	}

	var state = callSync("state") || {};
	var opened = {};                     // binary_content:// name -> ArrayBuffer
	var saved = !!state.saved;
	var no = function () { return false; };
	var nothing = function () {};

	var A = {
		features: {},

		// ---- the document -------------------------------------------------------------
		CreateEditorApi: function (api) { window.sgEditorApi = api; },
		IsLocalFile: function () { return true; },
		IsFilePrinting: no,
		CheckUserId: function () { return "sg-" + (state.id || "user").substring(0, 8); },
		LocalFileGetOpenChangesCount: function () { return 0; },
		// the document's own path (the editors show its file name as the title)
		LocalFileGetSourcePath: function () { return state.path || ""; },
		LocalFileGetRelativePath: function () { return ""; },
		LocalFileGetSaved: function () { return saved; },
		// a file in the open document's folder (the editors ask before fetching
		// optional parts, e.g. a spreadsheet's Editor.xlsx)
		IsLocalFileExist: function (path) {
			var prefix = (state.url || "") + "/";
			if (!path || path.indexOf(prefix) !== 0)
				return false;
			var r = callSync("exists", { name: path.substring(prefix.length) });
			return !!(r && r.exists);
		},
		GetOpenedFile: function (name) { return opened[name] || null; },

		LocalStartOpen: function () {
			if (state.debug) log("LocalStartOpen");
			callAsync("open", null, "").then(function (r) {
				if (state.debug) log("open: " + JSON.stringify(r));
				if (!r.ok)
					throw new Error(r.error || "open failed");
				return fetch(r.url + "/Editor.bin").then(function (b) {
					if (!b.ok) throw new Error("Editor.bin: " + b.status);
					return b.arrayBuffer();
				}).then(function (buf) {
					opened["binary_content://Editor.bin"] = buf;
					if (state.debug) log("Editor.bin: " + buf.byteLength + " bytes");
					window.DesktopOfflineAppDocumentEndLoad(r.url, "binary_content://Editor.bin", buf.byteLength);
				});
			}).catch(function (e) {
				log("open: " + e);
				window.DesktopOfflineAppDocumentEndLoad(state.url || "", "", 0);   // the editor shows its error
			});
		},

		// the editor's changes, in order; sent synchronously so a save that
		// follows sees every one of them
		LocalFileSaveChanges: function (changes, deleteIndex, count) {
			var entries = [];
			if (count > 0)
				entries = (typeof changes === "string") ? changes.split('","') : Array.prototype.slice.call(changes, 0, count);
			callSync("changes", null, JSON.stringify({ entries: entries, deleteIndex: deleteIndex }));
		},

		LocalFileSave: function (param, password, docinfo, fileType, jsonOptions) {
			var saveAs = /saveas=true/.test(param || "");
			callAsync("save", null, JSON.stringify({ saveAs: saveAs, fileType: fileType || 0, json: jsonOptions || "" }))
				.then(function (r) {
					if (r.ok) {
						saved = true;
						if (r.state) state = r.state;
					}
					// 0 = saved, 2 = the file could not be written; 1 = cancelled
					window.DesktopOfflineAppDocumentEndSave(r.ok ? 0 : (r.cancelled ? 1 : 2));
				}).catch(function (e) {
					log("save: " + e);
					window.DesktopOfflineAppDocumentEndSave(2);
				});
		},
		OnSave: nothing,

		onDocumentModifiedChanged: function (modified) {
			saved = !modified;
			callAsync("modified", { value: modified ? "1" : "0" }, "").catch(nothing);
		},
		SetDocumentName: nothing,
		SetLocalRestrictions: nothing,
		onDocumentContentReady: function () { log("ready"); },
		LocalFileRecents: nothing,

		// ---- pictures ------------------------------------------------------------------
		// a local picture the user inserts is copied into the document's folder
		LocalFileGetImageUrl: function (path) {
			var r = callSync("image", { path: path });
			return (r && r.name) ? r.name : "";
		},
		GetImageBase: nothing,
		IsImageFile: function (path) { return /\.(png|jpe?g|gif|bmp|svg|tiff?|webp)$/i.test(path || ""); },
		GetImageFormat: function (path) { var m = /\.([a-z]+)$/i.exec(path || ""); return m ? m[1].toLowerCase() : ""; },
		GetImageOriginalSize: function () { return { W: 0, H: 0 }; },
		GetDropFiles: function () { return []; },

		// ---- dialogs --------------------------------------------------------------------
		OpenFilenameDialog: function (filter, multi, callback) {
			if (typeof multi === "function") { callback = multi; multi = false; }
			callAsync("dialog-open", { filter: filter || "", multi: multi ? "1" : "0" }, "").then(function (r) {
				var files = r.files || [];
				callback && callback(multi ? files : (files[0] || ""));
			}).catch(function () { callback && callback(multi ? [] : ""); });
		},
		SaveFilenameDialog: function (filter, callback) { callback && callback(""); },

		// ---- the window and the interface -------------------------------------------------
		execCommand: function (cmd, param) {
			callAsync("command", null, JSON.stringify({ cmd: String(cmd), param: param === undefined ? "" : String(param) })).catch(nothing);
			return "";
		},
		GetSupportedScaleValues: function () { return [1, 1.25, 1.5, 1.75, 2]; },
		CheckNeedWheel: no,
		SetFullscreen: nothing,
		getViewportSettings: function () { return { widgetType: "window", captionHeight: 0 }; },
		sendSystemMessage: nothing,
		CallInAllWindows: nothing,
		startReporter: nothing, endReporter: nothing, sendToReporter: nothing, sendFromReporter: nothing,

		// ---- not offered by SG Office (yet): answered as "not supported" --------------------
		CryptoMode: 0,
		isBlockchainSupport: no,
		IsSignaturesSupport: no,
		IsProtectionSupport: no,
		isSupportPlugins: no,
		isSupportMacroses: no,
		IsSupportMedia: no,
		IsNativeViewer: no,
		IsCachedPdfCloudPrintFileInfo: no,
		// [system plugins, user plugins]: none
		GetInstallPlugins: function () { return JSON.stringify([{ url: "", pluginsData: [] }, { url: "", pluginsData: [] }]); },
		getDictionariesPath: function () { return ""; },
		SpellCheck: nothing,
		SetAdvancedOptions: nothing,
		GetEncryptedHeader: function () { return ""; },
		GetDefaultCertificate: function () { return ""; },
		GetFontThumbnailHeight: function () { return 28; },
		Print_Start: nothing, Print_Page: nothing, Print_End: nothing,
		Print: function () { log("print is not implemented yet"); }
	};

	// SG Office's look: Stained Glass OS's white surfaces and purple accent,
	// and each program's colour (the SG Office icons') on its header
	var sgLight = {
		id: "theme-sg-light", type: "light", name: "SG Light",
		colors: {
			"toolbar-header-document": "#2F6FD8",
			"toolbar-header-spreadsheet": "#239A5E",
			"toolbar-header-presentation": "#C84B16",
			"text-toolbar-header-on-background-document": "#1B4BA6",
			"text-toolbar-header-on-background-spreadsheet": "#126E40",
			"text-toolbar-header-on-background-presentation": "#A63C10",
			"background-toolbar": "#FFFFFF",
			"background-toolbar-additional": "#F7F7F7",
			"background-primary-dialog-button": "#7030C0",
			"highlight-primary-dialog-button-hover": "#8A4FD4",
			"background-accent-button": "#7030C0",
			"highlight-accent-button-hover": "#5E27A3",
			"highlight-accent-button-pressed": "#4B1F82",
			"border-control-focus": "#7030C0",
			"highlight-button-hover": "#EFEAF8",
			"highlight-button-pressed": "#DCD0F0",
			"highlight-button-pressed-hover": "#CFC0EA"
		}
	};

	window.AscDesktopEditor = A;
	window.RendererProcessVariable = window.RendererProcessVariable || {
		theme: { id: "theme-sg-light", type: "light", system: "light" },
		// the interface reads this as a list (Themes.js) and by id (desktopinit.js)
		localthemes: (function () { var a = [sgLight]; a[sgLight.id] = sgLight; return a; })(),
		rtl: false,
		os: "linux",
		helpUrl: ""
	};

	if (state.debug && window === window.top) {
		fetch("ascdesktop://fonts//usr/share/fonts/truetype/dejavu/DejaVuSans.ttf").then(function (r) {
			log("fetch font test: " + r.status + " " + r.type);
		}).catch(function (e) { log("fetch font test failed: " + e); });
		var x = new XMLHttpRequest();
		x.open("GET", "ascdesktop://fonts//usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", true);
		x.responseType = "arraybuffer";
		x.onload = function () { log("xhr font test: " + x.status + " " + (x.response && x.response.byteLength)); };
		x.onerror = function (e) { log("xhr font test error"); };
		x.send(null);
	}
	if (state.debug)
		setTimeout(function probe() {
			var e = window.Asc && Asc.editor;
			if (e)
			{
				var fl = (window.AscCommon && AscCommon.g_font_loader) || e.FontLoader || {};
				var pending = (fl.fonts_loading || []).map(function (f) { return f.Name; }).join(",");
				var files = (window.AscFonts && AscFonts.g_font_files) || [];
				var st = {};
				files.forEach(function (f) { st[f.Status] = (st[f.Status] || 0) + 1; });
				log("probe: full=" + e.isLoadFullApi + " docinfo=" + !!e.DocInfo + " modules=" + e.modulesLoaded + "/" + e.modulesCount
				    + " pending=[" + pending + "] files=" + JSON.stringify(st) + " loaded=" + e.isDocumentLoadComplete);
			}
			setTimeout(probe, 5000);
		}, 5000);

	window.addEventListener("unhandledrejection", function (e) {
		log("unhandled rejection: " + (e.reason && (e.reason.stack || e.reason.message) || e.reason));
	});
	window.addEventListener("error", function (e) {
		log("js error: " + e.message + " @ " + (e.filename || "") + ":" + (e.lineno || "") + ":" + (e.colno || "")
		    + (e.error && e.error.stack ? "\n" + String(e.error.stack).split("\n").slice(0, 8).join("\n") : ""));
	});
})();
