#!/usr/bin/env python3
"""Write the round-trip corpus -- one .docx, .xlsx and .pptx -- from scratch.

    ooxmlw.py OUTDIR

Minimal, valid ECMA-376 packages written by our own code (no Office-authored
templates, no binary fixtures in the repository), carrying the features
roundtrip.py checks survive a trip through the engine:

  .docx  a Heading 1 and a Heading 2, a paragraph with a bold run, a
         bulleted list (numbering.xml), a 2x2 table
  .xlsx  text and numbers, =SUM(B2:B3) and =TEXTJOIN(...) with no cached
         values (the engine must compute them), a #,##0.00 number format,
         a merged range A6:B6
  .pptx  a title slide with a subtitle, a slide with three bullets

Copyright (C) 2026 Stained Glass OS contributors
SPDX-License-Identifier: AGPL-3.0-or-later
"""
import os
import sys
import zipfile
from xml.sax.saxutils import escape

XML = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
PR = "http://schemas.openxmlformats.org/package/2006/relationships"
CT = "http://schemas.openxmlformats.org/package/2006/content-types"
W = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
S = "http://schemas.openxmlformats.org/spreadsheetml/2006/main"
P = "http://schemas.openxmlformats.org/presentationml/2006/main"
A = "http://schemas.openxmlformats.org/drawingml/2006/main"
RT = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
CTO = "application/vnd.openxmlformats-officedocument."
FIXED = (2026, 1, 1, 0, 0, 0)       # fixed timestamps: identical bytes every run


def rels(items):
    return XML + '<Relationships xmlns="%s">%s</Relationships>' % (PR, "".join(
        '<Relationship Id="%s" Type="%s%s" Target="%s"/>' % (i, RT, t, tgt) for i, t, tgt in items))


def types(defaults, overrides):
    return XML + '<Types xmlns="%s">%s%s</Types>' % (CT, "".join(
        '<Default Extension="%s" ContentType="%s"/>' % d for d in defaults), "".join(
        '<Override PartName="%s" ContentType="%s"/>' % o for o in overrides))


def write(path, parts):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in parts:
            z.writestr(zipfile.ZipInfo(name, FIXED), data)


BASE_DEFAULTS = [("rels", "application/vnd.openxmlformats-package.relationships+xml"),
                 ("xml", "application/xml")]


# ---- .docx --------------------------------------------------------------------------------------
def docx(path):
    def para(text, style=None, num=False):
        ppr = ""
        if style or num:
            ppr = "<w:pPr>%s%s</w:pPr>" % ('<w:pStyle w:val="%s"/>' % style if style else "",
                                          '<w:numPr><w:ilvl w:val="0"/><w:numId w:val="1"/></w:numPr>' if num else "")
        return "<w:p>%s<w:r><w:t xml:space=\"preserve\">%s</w:t></w:r></w:p>" % (ppr, escape(text))

    def cell(text):
        return '<w:tc><w:tcPr><w:tcW w:w="4500" w:type="dxa"/></w:tcPr>%s</w:tc>' % para(text)

    body = (para("SG Office round-trip: Documents", "Heading1") +
            '<w:p><w:r><w:t xml:space="preserve">The quick brown fox </w:t></w:r>'
            '<w:r><w:rPr><w:b/></w:rPr><w:t>jumps</w:t></w:r>'
            '<w:r><w:t xml:space="preserve"> over the lazy dog.</w:t></w:r></w:p>' +
            para("A list", "Heading2") +
            "".join(para(t, "ListParagraph", num=True) for t in ("alpha", "beta", "gamma")) +
            '<w:tbl><w:tblPr><w:tblW w:w="9000" w:type="dxa"/></w:tblPr>'
            '<w:tblGrid><w:gridCol w:w="4500"/><w:gridCol w:w="4500"/></w:tblGrid>'
            '<w:tr>%s%s</w:tr><w:tr>%s%s</w:tr></w:tbl>' % (cell("Name"), cell("Value"), cell("answer"), cell("42")) +
            '<w:p/><w:sectPr><w:pgSz w:w="12240" w:h="15840"/>'
            '<w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440" w:header="720" w:footer="720" w:gutter="0"/>'
            '</w:sectPr>')
    document = XML + '<w:document xmlns:w="%s" xmlns:r="%s"><w:body>%s</w:body></w:document>' % (W, R, body)

    def style(sid, name, extra="", ptype="paragraph"):
        return '<w:style w:type="%s" w:styleId="%s"><w:name w:val="%s"/>%s</w:style>' % (ptype, sid, name, extra)
    styles = XML + '<w:styles xmlns:w="%s">%s</w:styles>' % (W, "".join([
        '<w:docDefaults><w:rPrDefault><w:rPr><w:sz w:val="22"/></w:rPr></w:rPrDefault></w:docDefaults>',
        style("Normal", "Normal").replace('<w:style ', '<w:style w:default="1" ', 1),
        style("Heading1", "heading 1", '<w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/>'
              '<w:pPr><w:keepNext/><w:outlineLvl w:val="0"/></w:pPr><w:rPr><w:b/><w:sz w:val="32"/></w:rPr>'),
        style("Heading2", "heading 2", '<w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/>'
              '<w:pPr><w:keepNext/><w:outlineLvl w:val="1"/></w:pPr><w:rPr><w:b/><w:sz w:val="26"/></w:rPr>'),
        style("ListParagraph", "List Paragraph", '<w:basedOn w:val="Normal"/><w:pPr><w:ind w:left="720"/></w:pPr>'),
    ]))
    numbering = XML + ('<w:numbering xmlns:w="%s"><w:abstractNum w:abstractNumId="0">'
                       '<w:lvl w:ilvl="0"><w:start w:val="1"/><w:numFmt w:val="bullet"/>'
                       '<w:lvlText w:val="•"/><w:lvlJc w:val="left"/>'
                       '<w:pPr><w:ind w:left="720" w:hanging="360"/></w:pPr></w:lvl></w:abstractNum>'
                       '<w:num w:numId="1"><w:abstractNumId w:val="0"/></w:num></w:numbering>') % W
    write(path, [
        ("[Content_Types].xml", types(BASE_DEFAULTS, [
            ("/word/document.xml", CTO + "wordprocessingml.document.main+xml"),
            ("/word/styles.xml", CTO + "wordprocessingml.styles+xml"),
            ("/word/numbering.xml", CTO + "wordprocessingml.numbering+xml")])),
        ("_rels/.rels", rels([("rId1", "officeDocument", "word/document.xml")])),
        ("word/document.xml", document),
        ("word/_rels/document.xml.rels", rels([("rId1", "styles", "styles.xml"),
                                               ("rId2", "numbering", "numbering.xml")])),
        ("word/styles.xml", styles),
        ("word/numbering.xml", numbering)])


# ---- .xlsx --------------------------------------------------------------------------------------
def xlsx(path):
    def s(ref, text):
        return '<c r="%s" t="inlineStr"><is><t>%s</t></is></c>' % (ref, escape(text))

    def n(ref, v, style=None):
        return '<c r="%s"%s><v>%s</v></c>' % (ref, ' s="%d"' % style if style else "", v)

    def f(ref, formula):
        return '<c r="%s"><f>%s</f></c>' % (ref, escape(formula))     # no cached <v>: the engine computes
    rows = [
        (1, s("A1", "Item") + s("B1", "Qty")),
        (2, s("A2", "widgets") + n("B2", 3) + n("C2", 1234.5, style=1)),
        (3, s("A3", "gadgets") + n("B3", 4)),
        (4, f("B4", "SUM(B2:B3)")),
        (5, f("B5", '_xlfn.TEXTJOIN("-",TRUE,B2,B3)')),
        (6, s("A6", "merged footer")),
    ]
    sheet = XML + ('<worksheet xmlns="%s" xmlns:r="%s"><dimension ref="A1:C6"/><sheetData>%s</sheetData>'
                   '<mergeCells count="1"><mergeCell ref="A6:B6"/></mergeCells></worksheet>') % (
        S, R, "".join('<row r="%d">%s</row>' % r for r in rows))
    workbook = XML + ('<workbook xmlns="%s" xmlns:r="%s"><sheets>'
                      '<sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>') % (S, R)
    styles = XML + ('<styleSheet xmlns="%s"><numFmts count="1"><numFmt numFmtId="164" formatCode="#,##0.00"/></numFmts>'
                    '<fonts count="1"><font><sz val="11"/><name val="Carlito"/></font></fonts>'
                    '<fills count="2"><fill><patternFill patternType="none"/></fill>'
                    '<fill><patternFill patternType="gray125"/></fill></fills>'
                    '<borders count="1"><border><left/><right/><top/><bottom/><diagonal/></border></borders>'
                    '<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>'
                    '<cellXfs count="2"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>'
                    '<xf numFmtId="164" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1"/></cellXfs>'
                    '<cellStyles count="1"><cellStyle name="Normal" xfId="0" builtinId="0"/></cellStyles>'
                    '</styleSheet>') % S
    write(path, [
        ("[Content_Types].xml", types(BASE_DEFAULTS, [
            ("/xl/workbook.xml", CTO + "spreadsheetml.sheet.main+xml"),
            ("/xl/worksheets/sheet1.xml", CTO + "spreadsheetml.worksheet+xml"),
            ("/xl/styles.xml", CTO + "spreadsheetml.styles+xml")])),
        ("_rels/.rels", rels([("rId1", "officeDocument", "xl/workbook.xml")])),
        ("xl/workbook.xml", workbook),
        ("xl/_rels/workbook.xml.rels", rels([("rId1", "worksheet", "worksheets/sheet1.xml"),
                                             ("rId2", "styles", "styles.xml")])),
        ("xl/worksheets/sheet1.xml", sheet),
        ("xl/styles.xml", styles)])


# ---- .pptx --------------------------------------------------------------------------------------
def pptx(path):
    ns = 'xmlns:a="%s" xmlns:r="%s" xmlns:p="%s"' % (A, R, P)
    color = lambda name, rgb: '<a:%s><a:srgbClr val="%s"/></a:%s>' % (name, rgb, name)  # noqa: E731
    theme = XML + ('<a:theme xmlns:a="%s" name="SG"><a:themeElements><a:clrScheme name="SG">%s</a:clrScheme>'
                   '<a:fontScheme name="SG"><a:majorFont><a:latin typeface="Carlito"/><a:ea typeface=""/><a:cs typeface=""/></a:majorFont>'
                   '<a:minorFont><a:latin typeface="Carlito"/><a:ea typeface=""/><a:cs typeface=""/></a:minorFont></a:fontScheme>'
                   '<a:fmtScheme name="SG"><a:fillStyleLst>%s</a:fillStyleLst>'
                   '<a:lnStyleLst>%s</a:lnStyleLst><a:effectStyleLst>%s</a:effectStyleLst>'
                   '<a:bgFillStyleLst>%s</a:bgFillStyleLst></a:fmtScheme></a:themeElements></a:theme>') % (
        A, "".join(color(c, v) for c, v in [("dk1", "000000"), ("lt1", "FFFFFF"), ("dk2", "1F2A44"), ("lt2", "E7E6E6"),
                                             ("accent1", "2F6FB5"), ("accent2", "C0504D"), ("accent3", "9BBB59"),
                                             ("accent4", "8064A2"), ("accent5", "4BACC6"), ("accent6", "F79646"),
                                             ("hlink", "0563C1"), ("folHlink", "954F72")]),
        '<a:solidFill><a:schemeClr val="phClr"/></a:solidFill>' * 3,
        '<a:ln w="9525"><a:solidFill><a:schemeClr val="phClr"/></a:solidFill></a:ln>' * 3,
        '<a:effectStyle><a:effectLst/></a:effectStyle>' * 3,
        '<a:solidFill><a:schemeClr val="phClr"/></a:solidFill>' * 3)

    def sp(sid, name, ph, paras, xfrm=""):
        return ('<p:sp><p:nvSpPr><p:cNvPr id="%d" name="%s"/><p:cNvSpPr><a:spLocks noGrp="1"/></p:cNvSpPr>'
                '<p:nvPr><p:ph %s/></p:nvPr></p:nvSpPr><p:spPr>%s</p:spPr><p:txBody><a:bodyPr/><a:lstStyle/>%s'
                '</p:txBody></p:sp>') % (sid, name, ph, xfrm, "".join(
                    '<a:p><a:r><a:rPr lang="en-US"/><a:t>%s</a:t></a:r></a:p>' % escape(t) for t in paras)
                    or "<a:p/>")

    def tree(shapes):
        return ('<p:cSld><p:spTree><p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>'
                '<p:grpSpPr/>%s</p:spTree></p:cSld>') % shapes

    def box(x, y, cx, cy):
        return '<a:xfrm><a:off x="%d" y="%d"/><a:ext cx="%d" cy="%d"/></a:xfrm>' % (x, y, cx, cy)
    master = XML + ('<p:sldMaster %s>%s<p:clrMap bg1="lt1" tx1="dk1" bg2="lt2" tx2="dk2" accent1="accent1" '
                    'accent2="accent2" accent3="accent3" accent4="accent4" accent5="accent5" accent6="accent6" '
                    'hlink="hlink" folHlink="folHlink"/><p:sldLayoutIdLst><p:sldLayoutId id="2147483649" r:id="rId1"/>'
                    '<p:sldLayoutId id="2147483650" r:id="rId2"/></p:sldLayoutIdLst></p:sldMaster>') % (
        ns, tree(sp(2, "Title", 'type="title"', [], box(457200, 274638, 8229600, 1143000)) +
                 sp(3, "Body", 'type="body" idx="1"', [], box(457200, 1600200, 8229600, 4525963))))
    layout_title = XML + '<p:sldLayout %s type="title">%s<p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>' % (
        ns, tree(sp(2, "Title", 'type="ctrTitle"', [], box(685800, 2130425, 7772400, 1470025)) +
                 sp(3, "Subtitle", 'type="subTitle" idx="1"', [], box(1371600, 3886200, 6400800, 1752600))))
    layout_body = XML + '<p:sldLayout %s type="obj">%s<p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>' % (
        ns, tree(sp(2, "Title", 'type="title"', []) + sp(3, "Content", 'idx="1"', [])))
    slide1 = XML + '<p:sld %s>%s<p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sld>' % (
        ns, tree(sp(2, "Title 1", 'type="ctrTitle"', ["SG Office round-trip: Presentations"]) +
                 sp(3, "Subtitle 2", 'type="subTitle" idx="1"', ["A subtitle line"])))
    slide2 = XML + '<p:sld %s>%s<p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sld>' % (
        ns, tree(sp(2, "Title 1", 'type="title"', ["Bullets"]) +
                 sp(3, "Content 2", 'idx="1"', ["first", "second", "third"])))
    presentation = XML + ('<p:presentation %s><p:sldMasterIdLst><p:sldMasterId id="2147483648" r:id="rId1"/></p:sldMasterIdLst>'
                          '<p:sldIdLst><p:sldId id="256" r:id="rId2"/><p:sldId id="257" r:id="rId3"/></p:sldIdLst>'
                          '<p:sldSz cx="9144000" cy="6858000" type="screen4x3"/><p:notesSz cx="6858000" cy="9144000"/>'
                          '</p:presentation>') % ns
    to_layout = lambda k: rels([("rId1", "slideLayout", "../slideLayouts/slideLayout%d.xml" % k)])  # noqa: E731
    write(path, [
        ("[Content_Types].xml", types(BASE_DEFAULTS, [
            ("/ppt/presentation.xml", CTO + "presentationml.presentation.main+xml"),
            ("/ppt/slideMasters/slideMaster1.xml", CTO + "presentationml.slideMaster+xml"),
            ("/ppt/slideLayouts/slideLayout1.xml", CTO + "presentationml.slideLayout+xml"),
            ("/ppt/slideLayouts/slideLayout2.xml", CTO + "presentationml.slideLayout+xml"),
            ("/ppt/slides/slide1.xml", CTO + "presentationml.slide+xml"),
            ("/ppt/slides/slide2.xml", CTO + "presentationml.slide+xml"),
            ("/ppt/theme/theme1.xml", CTO + "theme+xml")])),
        ("_rels/.rels", rels([("rId1", "officeDocument", "ppt/presentation.xml")])),
        ("ppt/presentation.xml", presentation),
        ("ppt/_rels/presentation.xml.rels", rels([("rId1", "slideMaster", "slideMasters/slideMaster1.xml"),
                                                  ("rId2", "slide", "slides/slide1.xml"),
                                                  ("rId3", "slide", "slides/slide2.xml"),
                                                  ("rId4", "theme", "theme/theme1.xml")])),
        ("ppt/slideMasters/slideMaster1.xml", master),
        ("ppt/slideMasters/_rels/slideMaster1.xml.rels", rels([
            ("rId1", "slideLayout", "../slideLayouts/slideLayout1.xml"),
            ("rId2", "slideLayout", "../slideLayouts/slideLayout2.xml"),
            ("rId3", "theme", "../theme/theme1.xml")])),
        ("ppt/slideLayouts/slideLayout1.xml", layout_title),
        ("ppt/slideLayouts/_rels/slideLayout1.xml.rels", rels([("rId1", "slideMaster", "../slideMasters/slideMaster1.xml")])),
        ("ppt/slideLayouts/slideLayout2.xml", layout_body),
        ("ppt/slideLayouts/_rels/slideLayout2.xml.rels", rels([("rId1", "slideMaster", "../slideMasters/slideMaster1.xml")])),
        ("ppt/slides/slide1.xml", slide1),
        ("ppt/slides/_rels/slide1.xml.rels", to_layout(1)),
        ("ppt/slides/slide2.xml", slide2),
        ("ppt/slides/_rels/slide2.xml.rels", to_layout(2)),
        ("ppt/theme/theme1.xml", theme)])


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "corpus"
    os.makedirs(out, exist_ok=True)
    docx(os.path.join(out, "src.docx"))
    xlsx(os.path.join(out, "src.xlsx"))
    pptx(os.path.join(out, "src.pptx"))
    print("wrote src.docx, src.xlsx, src.pptx to %s" % out)


if __name__ == "__main__":
    main()
