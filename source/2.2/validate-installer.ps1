$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$schemaPath = Join-Path $root '../1.3.1/schema/ModConfig5.0.xsd'
$xmlPath = Join-Path $root 'installer/fomod/ModuleConfig.xml'
$settings = [Xml.XmlReaderSettings]::new()
$settings.Schemas.XmlResolver = [Xml.XmlUrlResolver]::new()
[void]$settings.Schemas.Add($null, (Resolve-Path $schemaPath).Path)
$settings.ValidationType = [Xml.ValidationType]::Schema
$settings.add_ValidationEventHandler({param($sender,$eventArgs) throw $eventArgs.Message})
$reader = [Xml.XmlReader]::Create($xmlPath, $settings)
try { while($reader.Read()){} } finally { $reader.Dispose() }
$text = [IO.File]::ReadAllText($xmlPath)
if(!$text.StartsWith('<?xml version="1.0" encoding="utf-8"?>')) { throw 'Unsafe Vortex XML declaration' }
$inputText = [regex]::Replace($text,'^([^>]*)encoding="[^"]+"','$1')
$stream = [IO.MemoryStream]::new()
$writer = [IO.StreamWriter]::new($stream,[Text.UnicodeEncoding]::new($true,$true))
$writer.Write($inputText); $writer.Flush(); $stream.Position=0
$reader = [Xml.XmlReader]::Create($stream)
try { while($reader.Read()){} } finally { $reader.Dispose(); $writer.Dispose(); $stream.Dispose() }
Write-Output 'PASS: FOMOD schema and Vortex encoding preprocessing'
