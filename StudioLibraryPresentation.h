// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <JuceHeader.h>

namespace studio::library
{
using namespace juce;
struct Presentation
{
    String name, family, portrait; // managed filename, never an arbitrary project path
};
inline File portraitDirectory()
{
    return File::getSpecialLocation(File::userMusicDirectory).getChildFile("Studio")
        .getChildFile("Instrument Library").getChildFile("Artwork");
}
inline File portraitFile(const String& filename,const File& folder=portraitDirectory())
{
    if(filename.isEmpty() || filename.containsChar('/') || filename.containsChar('\\')
        || filename.containsChar(':') || filename=="." || filename=="..")return {};
    auto f=folder.getChildFile(filename);
    return f.isAChildOf(folder)&&f.hasFileExtension("png;jpg;jpeg")&&!f.isSymbolicLink()?f:File();
}
inline Image previewImage(const File& source)
{
    if(!source.existsAsFile()||source.getSize()>8*1024*1024||!source.hasFileExtension("png;jpg;jpeg"))return {};
    auto image=ImageFileFormat::loadFrom(source);
    if(!image.isValid())return {};
    const auto scale=jmin(1.0f,jmin(700.0f/image.getWidth(),500.0f/image.getHeight()));
    if(scale<1)image=image.rescaled(jmax(1,roundToInt(scale*image.getWidth())),jmax(1,roundToInt(scale*image.getHeight())));
    return image;
}
inline Result retainPortrait(const File& original,String& filename,const File& folder=portraitDirectory())
{
    if(!previewImage(original).isValid())return Result::fail("Choose a readable PNG or JPEG artwork under 8 MiB.");
    if(auto r=folder.createDirectory();r.failed())return r;
    auto target=folder.getChildFile(Uuid().toString()+original.getFileExtension().toLowerCase());
    if(target.exists()||!original.copyFileTo(target))return Result::fail("Cannot retain artwork copy.");
    filename=target.getFileName();return Result::ok();
}
inline void writePresentation(tracktion::Plugin& plugin,const Presentation& p)
{
    plugin.state.setProperty("studioLibraryName",p.name,nullptr);
    plugin.state.setProperty("studioLibraryFamily",p.family,nullptr);
    plugin.state.setProperty("studioLibraryPortrait",p.portrait,nullptr);
}
inline Presentation readPresentation(const tracktion::Plugin& plugin)
{
    return {plugin.state.getProperty("studioLibraryName").toString(),
            plugin.state.getProperty("studioLibraryFamily").toString(),
            plugin.state.getProperty("studioLibraryPortrait").toString()};
}
}
