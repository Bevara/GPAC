<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<meta http-equiv="Content-Type" content="text/html; charset=iso-8859-1" />
<title>How to embed GPAC in a web page - GPAC Project on Advanced Content</title>
<link href="code/styles.css" rel="stylesheet" type="text/css" />
</head>
<body>
<div id="fond">
<?php include_once("nav.php"); ?>
<!-- =================== ENTETE DE LA PAGE =========================================  -->
	<div id="Chapeau_court">
		<h1>
      GPAC can be embedded in Mozilla-based web browsers since 0.4.0 (Osmozilla), and in MS Internet Explorer since 0.4.2 (GPAX), using the standard XHTML &lt;object&gt;. An interesting feature is the addition of javascript APIs to manipulate the plugin and the embedded scene.
		</h1>
	</div>
<!-- =================== CORPS DE LA PAGE ============================================  -->
	<div id="Centre_court">
			<!-- =================== SECTION 1 ============  -->
      <?php include_once("play_left.php"); ?>
			<!-- =================== SECTION 2 ============  -->
			<div class="Col2">
		<div id="sous_menu">
<ul>
<li><a href="#hyperlink">Hyperlinking</a></li>
<li><a href="#script">Scripting</a></li>
<li><a href="#params">Parameters</a></li>
<li><a href="#xhtml">Declaration</a></li>
</ul>
		</div>

<h1>Foreword</h1>
        <p>
GPAC can be embedded in HTML/XHTML web pages through an ActiveX control (GPAX) or a Mozilla plugin (Osmozillla). All features of GPAC clients are available in these plugins, like progressive loading (IsoMedia or progressive SAX on XMT, X3D or SVG), real-time streaming, web-radios support, etc. As usual, these features are controled through GPAC's <a href="doc_config.php">configuration file</a>.
<br>
To install the plugins: on Windows, use the GPAC installer because installation of mozilla plugin is a bit tricky; on other platforms simply use 'make install'.  
        </p>


<h1 id="xhtml">GPAC Declaration in (X)HTML</h1>

<p>As with any embedded object, GPAC can be declared through both <i>&lt;object&gt;</i> and <i>&lt;embed&gt;</i> tags in an HTML file. However, the result may differ depending on web browsers. In order to limit these problems, it is stringly recommended to only use declaration through an<i>&lt;object&gt;</i> tag.</p>

Depending on the web browser used, some attributes of the <i>&lt;object&gt;</i> element may be ignored, or even worse, the whole element may be ignored if some special attributes are used. When faced with such issues, web designers use nested  <i>&lt;object&gt;</i> and <i>&lt;embed&gt;</i> tags, but this is a rather heavy workaround. GPAC plugins have been designed to avoid such problems.

<h2>Declaration Syntax</h2>
<table class="xml_app">
<tbody><tr><td>&lt;object type="..." width="..." height="..."</td></tr>
<tr><td>classid="clsid:181D18E6-4DC1-4B55-B72E-BE2A10064995" &gt;</td></tr>
<tr><td>&lt;param name="..." value="..." /&gt;</td></tr>
<tr><td>...</td></tr>
<tr><td>&lt;param name="..." value="..." /&gt;</td></tr>
<tr><td>Your browser does not support GPAC ...</td></tr>
<tr><td>&lt;/object&gt;</td></tr>
</tbody></table>

<h2>Semantics</h2>
<ul>
<li><i>type</i> : identifies the content mime type. To make sure GPAC is loaded, use the private mime type <i>application/x-gpac</i>. GPAC plugins are by default registered to handle a good set of mime types:
<ul>
<li><i>application/x-gpac</i> : generic identifier for embedding GPAC.</li>
<li><i>application/mp4</i> : MPEG-4 movies with interaction</li>

<li><i>application/sdp</i> : Streaming presentations</li>
<li><i>audio/aac</i> : AAC audio (WebRadios)</li>
<li><i>audio/amr</i> : AMR audio (Furture WebRadios ??)</li>
<li><i>audio/mp4</i> : MP4 Audio (iTunes &amp; co)</li>

<li><i>audio/mpeg</i> : MP3 Audio (WebRadios)</li>
<li><i>image/svg+xml</i> : SVG document</li>
<li><i>model/vrml</i> : VRML world</li>
<li><i>model/x3d+vrml</i> : X3D world in VRML text format</li>
<li><i>model/x3d+xml</i> : X3D world in XML format</li>

<li><i>video/mp4</i> : MPEG-4 movie</li>
<li><i>video/3gpp</i> : 3GPP movie</li>
<li><i>video/3gpp2</i> : 3GPP2 movie</li>
<li><i>video/avi</i> : AVI movie</li>
<li><i>video/mpeg</i> : MPEG-1/2 movie</li>

</ul>
</li>
<li><i>width</i> , <i>height</i> : size of the GPAC window in your web page.</li>
<li><i>classid</i> : UUID identifier of GPAC ActiveX control. 
<ul>
<li>GPAC's classid is <b>181D18E6-4DC1-4B55-B72E-BE2A10064995</b></li>
<li>As a general rule, DO NOT SPECIFY THE CLASSID ATTRIBUTE, since mozilla browsers ignore <i>&lt;object&gt;</i> whenever a classid attribute is specified. The ActiveX plugin has been designed to avoid using the CLASSID, and relies on mime type associations.</li>

<li>The only case where you have to use classid is for Pocket Internet Explorer, which currently does not handle mime type associations for ActiveX controls.</li>
</ul>
</li>
</ul>

<h1 id="params">Parameters for GPAC plugins</h1>
<p>Plugin parameters are declared with the <i>&lt;param&gt;</i> tag. <b>DO NOT</b> try to declare parameters as attributes of the <i>&lt;object&gt;</i>, behaviour may differ upon browsers. The following parameters are used:</p>

<ul>
<li><i>"src"</i> : location of the media ressource to play with GPAC. Any URL supported by GPAC can be specified here. The URL may be absolute or relative.</li>
<li><i>"use3d"</i> : indicates that GPAC 3D renderer is needed to play the content. This overrides the GPAC configuration variable, allowing co-existence of several plugins in a same page with different renderers. Value is either <i>true</i> or <i>false</i>, the default value being <i>false</i>.</li>
<li><i>"autostart"</i> : specifies if content shall start playing upon load of the plugin or not. Value is either <i>true</i> or <i>false</i>, the default value being <i>true</i>. If the value is <i>false</i>, scripting will be required to trigger playback.</li>

<li><i>"aspectratio"</i> : specifies how aspect ratio shall be preserved by GPAC. This parameter can take the following values:
<ul>
<li><i>keep</i> : aspect ratio is preserved.</li>
<li><i>16:9</i> : forces usage of a 16:9 aspect ratio.</li>
<li><i>4:3</i> : forces usage of a 4:3 aspect ratio.</li>
<li><i>fill</i> : content is stretched to fill the entire object display area, without aspect ratio preservation.</li>

</ul>
</li> 
</ul>

<br/>


<h1 id="script">Scripting the plugin</h1>
<p>
The browser plugins may be controled dynamically from the HTML page through scripting. The current scripting API is very basic and will be enhanced in the near future. Do not hesitate to <a href="http://sourceforge.net/tracker/?group_id=84101&amp;atid=571741">send us suggestions/requests</a> for the scripting side.
</p>

<h2>Setting up the plugin for scripting</h2>

<p>All you have to do is assign an ID to the GPAC object element, and use it in JavaScript calls.</p>

<table class="xml_app">
<tbody><tr><td>&lt;object width="100" height="100" type="application/x-gpac" id="gpac" &gt;</td></tr>
<tr><td>&lt;param name="src" value="file.mp4" /&gt;</td></tr>
<tr><td>&lt;/object&gt;</td></tr>
<tr><td></td></tr>
<tr><td>&lt;input onclick="gpac.Pause();" value="Pause" type="button" /&gt;</td></tr>
</tbody></table>

<br/>
<h2>Plugins Methods</h2>
<p>The following methods are defined in the plugins:</p>

<ul>
<li><i>Play()</i> : connects the current URL if needed, and plays it. This is mainly used when the plugin has been declared with <i>autostart</i> set to <i>false</i>. Function doesn't take any parameter.</li>
<li><i>Stop()</i> : disconnects the current URL. Function doesn't take any parameter.</li>
<li><i>Pause()</i> : plays/pause media playback. Function doesn't take any parameter.</li>

<li><i>Update(type, scene_data)</i> : sends an update to GPAC. Updates can be any kind of updates supported by GPAC scene manager (BT, XMT, LASeR+XML), or a complete scene. Parameters are:
<ul>
<li>type : mime type of the update language used. Accepted values are:
<ul>
<li><i>application/x-bt</i> : update is a BT scene or a set of BT commands.</li>
<li><i>application/x-xmt</i> : update is a XMT scene or a set of XMT commands.</li>
<li><i>model/vrml</i> : update is a VRML world.</li>

<li><i>model/x3d+vrml</i> : update is an X3D world in VRML format.</li>
<li><i>model/x3d+xml</i> : update is an X3D world in XML format.</li>
<li><i>image/svg+xml</i> : update is an SVG document.</li>
<li><i>application/x-laser+xml</i> : update is a LASeR document or a set of LASeR updates, in XML format.</li>
</ul>

</li>
<li>scene_data : the textual scene data to process.</li>
</ul>
<br/>
<p>Note that this assumes you have some knowledge of the scene being played by GPAC, since you will have to access scene nodes by their IDs. For example, consider the plugin is playing a BT file showing a rectangle with a Material2D defined as <b>MAT</b>. You may control the material in BT as follows:</p>

<table class="xml_app">
<tbody><tr><td>&lt;object width="100" height="100" type="application/x-gpac" id="gpac" &gt;</td></tr>
<tr><td>&lt;param name="src" value="rect.bt" /&gt;</td></tr>
<tr><td>&lt;/object&gt;</td></tr>
<tr><td></td></tr>
<tr><td>&lt;input onclick="gpac.Update('application/x-bt', 'REPLACE MAT.emissiveColor BY 1 0 0');" value="red" type="button" /&gt;</td></tr>

<tr><td>&lt;input onclick="gpac.Update('application/x-bt', 'REPLACE MAT.emissiveColor BY 0 0 1');" value="blue" type="button" /&gt;</td></tr>
</tbody></table>

</li>
</ul>


<br/>
<h1 id="hyperlink">Hyperlinking handling</h1>
<p>Whenever an hyperlink is clicked in an embedded GPAC object (MPEG-4/VRML <i>Anchor</i>, SVG <i>a</i>), the following process occurs:</p>
<ul>

<li>If GPAC supports the URL, any parameter associated with the hyperlink are ignored, GPAC just replaces the current presentation with the given URL.</li>
<li>If GPAC doesn't support the URL, it gives it to the web browser with the associated parameter (_parent, _blank, _new, _top, _target=FrameID). Note that this is not supported for Pocket Internet Explorer at the current time.

</li>
</ul>
      </div>
	</div>
<?php $mod_date="\$Date: 2008-04-11 09:48:30 $"; ?>
<?php include_once("bas.php"); ?>
<!-- =================== FIN CADRE DE LA PAGE =========================================  -->
</div>
</body>
</html>
