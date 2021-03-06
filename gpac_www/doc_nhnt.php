<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<meta http-equiv="Content-Type" content="text/html; charset=iso-8859-1" />
<title>NHNT and NHML Documentation - GPAC Project on Advanced Content</title>
<link href="code/styles.css" rel="stylesheet" type="text/css" />
</head>
<body>
<div id="fond">
<?php include_once("nav.php"); ?>
<!-- =================== ENTETE DE LA PAGE =========================================  -->
	<div id="Chapeau_court">
		<h1>
MP4Box can natively import many multimedia formats (AVI, MP3, ...) into ISO files. For non-natively supported media formats, MP4Box defines two generic multiplexing languages: a binary format NHNT and a more powerful XML language called NHML.
		</h1>
	</div>
<!-- =================== CORPS DE LA PAGE ============================================  -->
	<div id="Centre_court">
      <?php include_once("pack_left.php"); ?>
			<div class="Col2">

		<div id="sous_menu">
<ul>
<li><a href="#NHML">NHML Format</a></li>
<li><a href="#NHNT">NHNT Format</a></li>
</ul>
		</div>

  <h1 id="NHNT">NHNT Format</h1>
<p>
The NHNT format has been developed during the MPEG-4 Systems implementation phase, as a way to easily mux unknown media formats to an MP4 file or an MPEG-4 multiplex.
The goal was to have the media encoder produce a description of the media time fragmentation (access units and timestamps) that could be reused by a media-unaware MPEG-4 multiplexer.
</p>
A NHNT source is composed of 2 or 3 parts:
<ul>
<li><b>the media file</b>: This file contains all the media data as written by the encoder. The file extension must be <i>.media</i>.</li>

<li><b>the NHNT (meta) file</b>: This file contains all the information needed by the MPEG-4 multiplexer to use the media data. The file extension must be <i>.nhnt</i>. </li>
<li><b>the decoder initialization file</b>: If the media format requires decoder configuration data (MPEG-4 Visual, AAC, AVC/H264, ...), the binary data is put in this third file in order for the MPEG-4 multiplexer to correctly signal decoder configuration. 
This is required by the fact that in MPEG-4 Systems, configuration data is never sent in-band of the media stream, but through the object descriptor stream. The file extension must be <i>.info</i>.</li>
</ul>


<h2>The NHNT file format</h2>
A NHNT file is made of a header, and a set of access units descriptors. All integers are written in network-byte order.

<h3>Header Syntax</h3>

<table class="xml_app">
<tbody><tr><td>char Signature[4];</td></tr>
<tr><td>bit(8) version;</td></tr>
<tr><td>bit(8) streamType;</td></tr>
<tr><td>bit(8) objectTypeIndication;</td></tr>
<tr><td>bit(16) reserved = 0;</td></tr>
<tr><td>bit(24) bufferSizeDB;</td></tr>
<tr><td>bit(32) avgBitRate;</td></tr>
<tr><td>bit(32) maxBitRate;</td></tr>

<tr><td>bit(32) timeStampResolution;</td></tr>
</tbody></table>

<h3>Semantics</h3>
<ul>
<li><i>Signature</i> : identifies the file as an NHNT file. The signature must be 'NHnt' or 'NHnl' for large files (using 64 bits offsets and timestamps).</li>
<li><i>version</i> : identifies the NHNT version used to produce the file. Default version is 0.</li>
<li><i>streamType</i> : identifies the media streamType as specified in MPEG-4 (0x04: Visual, 0x05: audio, ...). Officially supported stream types are listed <a href="http://www.mp4ra.org/object.html">here</a>.</li>

<li><i>objectTypeIndication</i> : identifies the media type as specified in MPEG-4. For example, 0x40 for MPEG-4 AAC. Officially supported object types are listed <a href="http://www.mp4ra.org/object.html">here</a>.</li>
<li><i>bufferSizeDB</i> : indicates the size of the decoding buffer for this stream in byte.</li>
<li><i>avgBitRate</i> : indicates the average bitrate in bits per second of this elementary stream. For streams with variable bitrate this value shall be set to zero.</li>
<li><i>maxBitRate</i> : indicates the maximum bitrate in bits per second of this elementary stream in any time window of one second duration.</li>

<li><i>timeStampResolution</i> : indicates the unit in which the media timestamps are expressed in the file (<i>timeStampResolution</i> ticks = 1 second).</li>
</ul>

<p>After the header, the file is just a succession of access unit (sample) info until the end of the file.</p>

<h3>Sample Header Syntax for 'NHnt' files</h3>
<table class="xml_app">
<tbody><tr><td>bit(24) data_size;</td></tr>

<tr><td>bit(1) random_access_point;</td></tr>
<tr><td>bit(1) au_start_flag;</td></tr>
<tr><td>bit(1) au_end_flag;</td></tr>
<tr><td>bit(3) reserved = 0;</td></tr>
<tr><td>bit(2) frame_type;</td></tr>
<tr><td>bit(32) file_offset;</td></tr>
<tr><td>bit(32) compositionTimeStamp;</td></tr>
<tr><td>bit(32) decodingTimeStamp;</td></tr>
</tbody></table>

<h3>Sample Header Syntax for 'NHnl' files</h3>
<table class="xml_app">
<tbody><tr><td>bit(24) data_size;</td></tr>
<tr><td>bit(1) random_access_point;</td></tr>
<tr><td>bit(1) au_start_flag;</td></tr>
<tr><td>bit(1) au_end_flag;</td></tr>
<tr><td>bit(3) reserved = 0;</td></tr>
<tr><td>bit(2) frame_type;</td></tr>
<tr><td>bit(64) file_offset;</td></tr>

<tr><td>bit(64) compositionTimeStamp;</td></tr>
<tr><td>bit(64) decodingTimeStamp;</td></tr>
</tbody></table>

<h3>Semantics</h3>
<ul>
<li><i>data_size</i> : indicates the amount of data to fetch from the source file for this access unit.</li>
<li><i>random_access_point</i> : indicates if the access unit is a random access point.</li>

<li><i>au_start_flag</i> : indicates if this is the start of an access unit or not.</li>
<li><i>au_end_flag</i> : indicates if this is the end of an access unit or not.</li>
<li><i>frame_type</i> : Used for bidirectional video coding sources only, 0 otherwise.
<ul>
<li>frame_type=2: access unit is a B-frame</li>
<li>frame_type=1: access unit is a P-frame</li>
<li>frame_type=0: access unit is an I-frame</li>

</ul>
</li>
<li><i>file_offset</i> : indicates the position in the source file of the first byte to fetch for this data chunk.</li>
<li><i>compositionTimeStamp</i> : indicates the composition (presentation) time stamp of this access unit.</li>
<li><i>decodingTimeStamp</i> : indicates the decoding time stamp of this access unit.</li>
</ul>

<p><b>Note</b> : Samples must be described in decoding order in the nhnt file when using sample fragmentation. Otherwise, sample may be described out of order.</p>

<a name="NHML"></a>
<h1 id="NHML">NHML Format</h1>
<p>
The NHNT format is a very usefull tool for multiplexing data, but is not user-friendly at all when dealing with complex cases such as multi-source media files or NHNT authoring (timing modification, data removal or insertion).
</p>
<p>The NHML format has been therefore developed at ENST in order to provide more control about the imported data source and give the user the tools to easily modify the multiplexing process. </p>
<p>The NHML format is an XML-based description of a media file, just like NHNT, with some major enhancements. This format is supported since GPAC 0.4.2.</p>
<p>To obtain some sample NHML files, simply use <i>MP4Box -nhml trackID srcFile</i></p>

<h2>The NHML file format</h2>

Just like any XML file, the file must begin with the usual xml header. The file encoding SHALL BE UTF-8. 
<p>The root element of an NHML file is the <i>NHNTStream</i></p>

<h3>Syntax</h3>
<table class="xml_app">
<tbody><tr><td>&lt;NHNTStream 
baseMediaFile="..." 
specificInfoFile="..." 
trackID="..." 
inRootOD="..." 
DTS_increment="..." 
timeScale="..." 
streamType="..." 
objectTypeIndication="..." 
mediaType="..." 
mediaSubType="..." 
width="..." 
height="..." 
parNum="..." 
parDen="..." 
sampleRate="..." 
numChannels="..." 
bitsPerSample="..."
compressorName="..." 
codecVersion="..." 
codecRevision="..." 
codecVendor="..." 
temporalQuality="..." 
spatialQuality="..."
horizontalResolution="..."
verticalResolution="..."
bitDepth="..."
&gt;</td></tr>

<tr style="text-indent: 5%;"><td>&lt;NHNTSample /&gt;</td></tr>
<tr style="text-indent: 5%;"><td>...</td></tr>
<tr style="text-indent: 5%;"><td>&lt;NHNTSample /&gt;</td></tr>

<tr><td>&lt;/NHNTStream&gt;</td></tr>
</tbody></table>

<h3>Semantics</h3>
<ul>
<li><i>baseMediaFile</i> : indicates the default location of the stream data. If not set, the file with the same name and extension <i>.media</i> is assumed to be the source.</li>
<li><i>specificInfoFile</i> : indicates the location of the decoder configuration data if any.</li>

<li><i>trackID</i> : indicates a desired trackID for this media when importing to IsoMedia. Value type: unsigned integer. Default Value: 0.</li>
<li><i>inRootOD</i> : indicates if the imported stream is present in the InitialObjectDescriptor. Value type: "yes", "no". Default Value: "no".</li>
<li><i>DTS_increment</i> : indicates a default time increment between two consecutive samples. Value type: unsigned integer. Default Value: 0.</li>
<li><i>timeScale</i> : indicates the time scale in which the time stamps are given. Value type: unsigned integer. Default Value: 1000 or sample rate if specified.</li>
<li><i>streamType</i> : identifies the media streamType as specified in MPEG-4 (0x04: Visual, 0x05: audio, ...). Officially supported stream types are listed <a href="http://www.mp4ra.org/object.html">here</a>.</li>

<li><i>objectTypeIndication</i> : identifies the media type as specified in MPEG-4. For example, 0x40 for MPEG-4 AAC. Officially supported object types are listed <a href="http://www.mp4ra.org/object.html">here</a>.</li>
<li><i>mediaType</i> : indicates the 4CC media type (handler) as used in IsoMedia. Not needed if streamType is specified. Value Type: 4 byte string. Officially supported handler types are listed <a href="http://www.mp4ra.org/handler.html">here</a>.</li>
<li><i>mediaSubType</i> : indicates the 4CC media subtype (codec) to use in IsoMedia. This subtype will identify the sample description used (stsd table). Not needed if streamType is specified. Value Type: 4 byte string. Officially supported codec types are listed <a href="http://www.mp4ra.org/codecs.html">here</a>.</li>

<li><i>width</i>, <i>height</i> : indicates the dimension of a visual media. Ignored if the media is not video (streamType 0x04 or mediaType "vide"). Value Type: unsigned integer.</li>
<li><i>parNum</i>, <i>parDen</i> : indicates the pixel aspect ratio of a visual media. Ignored if the media is not video (streamType 0x04 or mediaType "vide"). Value Type: unsigned integer.</li>
<li><i>sampleRate</i> : indicates the sample rate of an audio media. Ignored if the media is not audio (streamType 0x05 or mediaType "soun"). Value Type: unsigned integer.</li>
<li><i>numChannels</i> : indicates the number of channels of an audio media. Ignored if the media is not audio (streamType 0x05 or mediaType "soun"). Value Type: unsigned integer.</li>

<li><i>bitsPerSample</i> : indicates the number of bits per audio sample for an audio media. Ignored if the media is not audio (streamType 0x05 or mediaType "soun"). Value Type: unsigned integer.</li>
</ul>

<p>All other parameters are used when creating custum sample description in IsoMedia (eg, not using MPEG-4 streamType and ObjectTypeIndication). Their semantics are given in the QT (and IsoMedia) file format specification.</p>
Each access unit is then described with a <i>NHNTSample</i> element.

<h3>Syntax</h3>
<table class="xml_app">
<tbody><tr><td>&lt;NHNTSample 
DTS="..." 
CTSOffset="..." 
isRAP="..." 
isSyncShadow="..." 
mediaOffset="..." 
dataLength="..." 
mediaFile="..." 
xmlFrom="..." 
xmlTo="..." 
/&gt;</td></tr>

</tbody></table>

<h3>Semantics</h3>
<ul>
<li><i>DTS</i> : decoding time stamp of the sample. If not set, the previous sample DTS (or 0) plus the specified <i>DTS_increment</i> is used. Value type: unsigned integer. Default Value: 0.</li>
<li><i>CTSOffset</i> : offset between the decoding and the composition time stamp of the sample. Value type: unsigned integer. Default Value: 0.</li>
<li><i>isRAP</i> : indicates if the sample is a random access point or not. Value type: "yes", "no". Default Value: "no".</li>

<li><i>isSyncShadow</i> : indicates if the sample is a sync shadow sample (IsoMedia storage only). Value type: "yes", "no". Default Value: "no".</li>
<li><i>mediaOffset</i> : indicates the position of the first byte of this sample in the media source file. Value type: unsigned integer. Default Value: 0.</li>
<li><i>dataLength</i> : indicates the size of this sample. Value type: unsigned integer. Default Value: 0.</li>
<li><i>mediaFile</i> : indicates the media source file to use. If not set, the <i>baseMediaFile</i> is used.</li>

<li><i>xmlFrom</i> : if the source file is XML data, indicates the location of the first element to copy fom the XML document. The location can be "doc.start", "elt_id.start" or "elt_id.end". Elements are idendified through their "id", "xml:id" or "DEF" attributes.</li>
<li><i>xmlTo</i> : if the source file is XML data, indicates the location of the last element to copy fom the XML document. The location can be "doc.end", "elt_id.start" or "elt_id.end". Elements are idendified through their "id", "xml:id" or "DEF" attributes.</li>
</ul>


<p></p>		</div>
	</div>

<?php $mod_date="\$Date: 2007-08-30 13:19:19 $"; ?>
<?php include_once("bas.php"); ?>
<!-- =================== FIN CADRE DE LA PAGE =========================================  -->
</div>
</body>
</html>

