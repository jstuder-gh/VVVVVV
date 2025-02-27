#if !defined(NO_CUSTOM_LEVELS)

#define CL_DEFINITION
#include "CustomLevels.h"

#include <physfs.h>
#include <stdio.h>
#include <string>
#include <tinyxml2.h>
#include <utf8/unchecked.h>

#include "Constants.h"
#include "Editor.h"
#include "Enums.h"
#include "FileSystemUtils.h"
#include "Game.h"
#include "Graphics.h"
#include "GraphicsUtil.h"
#include "KeyPoll.h"
#include "Map.h"
#include "Script.h"
#include "UtilityClass.h"
#include "Vlogging.h"
#include "XMLUtils.h"

#ifdef _WIN32
#define SCNx32 "x"
#define SCNu32 "u"
#else
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif
#include <inttypes.h>
#endif

#define VMULT(y) (y * SCREEN_WIDTH_TILES * maxwidth)
#define Y_INBOUNDS(y) (y >= 0 && y < SCREEN_HEIGHT_TILES * maxheight)

RoomProperty::RoomProperty(void)
{
    tileset=0;
    tilecol=0;
    warpdir=0;
    platx1=0;
    platy1=0;
    platx2=320;
    platy2=240;
    platv=4;
    enemyx1=0;
    enemyy1=0;
    enemyx2=320;
    enemyy2=240;
    enemytype=0;
    directmode=0;
}

customlevelclass::customlevelclass(void)
{
    reset();
}

// comparison, not case sensitive.
static bool compare_nocase (std::string first, std::string second)
{
    unsigned int i=0;
    while ( (i<first.length()) && (i<second.length()) )
    {
        if (SDL_tolower(first[i])<SDL_tolower(second[i]))
            return true;
        else if (SDL_tolower(first[i])>SDL_tolower(second[i]))
            return false;
        ++i;
    }
    if (first.length()<second.length())
        return true;
    else
        return false;
}

static void levelZipCallback(const char* filename)
{
    if (!FILESYSTEM_isFile(filename))
    {
        return;
    }

    if (endsWith(filename, ".zip"))
    {
        FILESYSTEM_loadZip(filename);
    }
}

void customlevelclass::loadZips(void)
{
    FILESYSTEM_enumerateLevelDirFileNames(levelZipCallback);
}

static void replace_all(std::string& str, const std::string& from, const std::string& to)
{
    if (from.empty())
    {
        return;
    }

    size_t start_pos = 0;

    while ((start_pos = str.find(from, start_pos)) != std::string::npos)
    {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); //In case `to` contains `from`, like replacing 'x' with 'yx'
    }
}

static std::string find_tag(const std::string& buf, const std::string& start, const std::string& end)
{
    size_t tag = buf.find(start);

    if (tag == std::string::npos)
    {
        //No start tag
        return "";
    }

    size_t tag_start = tag + start.size();
    size_t tag_close = buf.find(end, tag_start);

    if (tag_close == std::string::npos)
    {
        //No close tag
        return "";
    }

    size_t tag_len = tag_close - tag_start;
    std::string value(buf.substr(tag_start, tag_len));

    //Encode special XML entities
    replace_all(value, "&quot;", "\"");
    replace_all(value, "&amp;", "&");
    replace_all(value, "&apos;", "'");
    replace_all(value, "&lt;", "<");
    replace_all(value, "&gt;", ">");

    //Encode general XML entities
    size_t start_pos = 0;
    while ((start_pos = value.find("&#", start_pos)) != std::string::npos)
    {
        if (start_pos + 2 >= value.length())
        {
            return "";
        }

        bool hex = value[start_pos + 2] == 'x';
        size_t end = value.find(';', start_pos);

        if (end == std::string::npos)
        {
            return "";
        }

        size_t real_start = start_pos + 2 + ((int) hex);
        std::string number(value.substr(real_start, end - real_start));

        if (!is_positive_num(number.c_str(), hex))
        {
            return "";
        }

        uint32_t character = 0;
        if (hex)
        {
            SDL_sscanf(number.c_str(), "%" SCNx32, &character);
        }
        else
        {
            SDL_sscanf(number.c_str(), "%" SCNu32, &character);
        }
        uint32_t utf32[] = {character, 0};
        std::string utf8;
        utf8::unchecked::utf32to8(utf32, utf32 + 1, std::back_inserter(utf8));
        value.replace(start_pos, end - start_pos + 1, utf8);
    }

    return value;
}

#define TAG_FINDER(NAME, TAG) \
static std::string NAME(const std::string& buf) \
{ \
    return find_tag(buf, "<" TAG ">", "</" TAG ">"); \
}

TAG_FINDER(find_metadata, "MetaData") //only for checking that it exists

TAG_FINDER(find_creator, "Creator")
TAG_FINDER(find_title, "Title")
TAG_FINDER(find_desc1, "Desc1")
TAG_FINDER(find_desc2, "Desc2")
TAG_FINDER(find_desc3, "Desc3")
TAG_FINDER(find_website, "website")

#undef TAG_FINDER

static void levelMetaDataCallback(const char* filename)
{
    extern customlevelclass cl;
    LevelMetaData temp;
    std::string filename_ = filename;

    if (!endsWith(filename, ".vvvvvv")
    || !FILESYSTEM_isFile(filename)
    || FILESYSTEM_isMounted(filename))
    {
        return;
    }

    if (cl.getLevelMetaData(filename_, temp))
    {
        cl.ListOfMetaData.push_back(temp);
    }
}

void customlevelclass::getDirectoryData(void)
{

    ListOfMetaData.clear();

    FILESYSTEM_clearLevelDirError();

    loadZips();

    FILESYSTEM_enumerateLevelDirFileNames(levelMetaDataCallback);

    for(size_t i = 0; i < ListOfMetaData.size(); i++)
    {
        for(size_t k = 0; k < ListOfMetaData.size(); k++)
        {
            if(compare_nocase(ListOfMetaData[i].title, ListOfMetaData[k].title ))
            {
                std::swap(ListOfMetaData[i] , ListOfMetaData[k]);
            }
        }
    }

}
bool customlevelclass::getLevelMetaData(const std::string& _path, LevelMetaData& _data )
{
    unsigned char *uMem;
    FILESYSTEM_loadFileToMemory(_path.c_str(), &uMem, NULL, true);

    if (uMem == NULL)
    {
        vlog_warn("Level %s not found :(", _path.c_str());
        return false;
    }

    std::string buf((char*) uMem);

    FILESYSTEM_freeMemory(&uMem);

    if (find_metadata(buf) == "")
    {
        vlog_warn("Couldn't load metadata for %s", _path.c_str());
        return false;
    }

    _data.creator = find_creator(buf);
    _data.title = find_title(buf);
    _data.Desc1 = find_desc1(buf);
    _data.Desc2 = find_desc2(buf);
    _data.Desc3 = find_desc3(buf);
    _data.website = find_website(buf);

    _data.filename = _path;
    return true;
}

void customlevelclass::reset(void)
{
    version=2; //New smaller format change is 2

    mapwidth=5;
    mapheight=5;

    title="Untitled Level";
    creator="Unknown";

    levmusic=0;

    customentities.clear();
    levmusic=0;

    for (int j = 0; j < maxheight; j++)
    {
        for (int i = 0; i < maxwidth; i++)
        {
            roomproperties[i+(j*maxwidth)].tileset=0;
            roomproperties[i+(j*maxwidth)].tilecol=(i+j)%32;
            roomproperties[i+(j*maxwidth)].roomname="";
            roomproperties[i+(j*maxwidth)].warpdir=0;
            roomproperties[i+(j*maxwidth)].platx1=0;
            roomproperties[i+(j*maxwidth)].platy1=0;
            roomproperties[i+(j*maxwidth)].platx2=320;
            roomproperties[i+(j*maxwidth)].platy2=240;
            roomproperties[i+(j*maxwidth)].platv=4;
            roomproperties[i+(j*maxwidth)].enemyx1=0;
            roomproperties[i+(j*maxwidth)].enemyy1=0;
            roomproperties[i+(j*maxwidth)].enemyx2=320;
            roomproperties[i+(j*maxwidth)].enemyy2=240;
            roomproperties[i+(j*maxwidth)].enemytype=0;
            roomproperties[i+(j*maxwidth)].directmode=0;
        }
    }

    SDL_zeroa(contents);

    script.clearcustom();

    onewaycol_override = false;
}

const int* customlevelclass::loadlevel( int rxi, int ryi )
{
    //Set up our buffer array to be picked up by mapclass
    rxi -= 100;
    ryi -= 100;
    if(rxi<0)rxi+=mapwidth;
    if(ryi<0)ryi+=mapheight;
    if(rxi>=mapwidth)rxi-=mapwidth;
    if(ryi>=mapheight)ryi-=mapheight;

    static int result[1200];

    for (int j = 0; j < 30; j++)
    {
        for (int i = 0; i < 40; i++)
        {
            result[i + j*40] = gettile(rxi, ryi, i, j);
        }
    }

    return result;
}

int customlevelclass::getlevelcol(const int tileset, const int tilecol)
{
    if(tileset==0)  //Space Station
    {
        return tilecol;
    }
    else if(tileset==1)   //Outside
    {
        return 32+tilecol;
    }
    else if(tileset==2)   //Lab
    {
        return 40+tilecol;
    }
    else if(tileset==3)   //Warp Zone
    {
        return 46+tilecol;
    }
    else if(tileset==4)   //Ship
    {
        return 52+tilecol;
    }
    return 0;
}

int customlevelclass::getenemycol(int t)
{
    switch(t)
    {
        //RED
    case 3:
    case 7:
    case 12:
    case 23:
    case 28:
    case 34:
    case 42:
    case 48:
    case 58:
        return 6;
        break;
        //GREEN
    case 5:
    case 9:
    case 22:
    case 25:
    case 29:
    case 31:
    case 38:
    case 46:
    case 52:
    case 53:
        return 7;
        break;
        //BLUE
    case 1:
    case 6:
    case 14:
    case 27:
    case 33:
    case 44:
    case 50:
    case 57:
        return 12;
        break;
        //YELLOW
    case 4:
    case 17:
    case 24:
    case 30:
    case 37:
    case 45:
    case 51:
    case 55:
        return 9;
        break;
        //PURPLE
    case 2:
    case 11:
    case 15:
    case 19:
    case 32:
    case 36:
    case 49:
        return 20;
        break;
        //CYAN
    case 8:
    case 10:
    case 13:
    case 18:
    case 26:
    case 35:
    case 41:
    case 47:
    case 54:
        return 11;
        break;
        //PINK
    case 16:
    case 20:
    case 39:
    case 43:
    case 56:
        return 8;
        break;
        //ORANGE
    case 21:
    case 40:
        return 17;
        break;
    default:
        return 6;
        break;
    }
}

int customlevelclass::getwarpbackground(int rx, int ry)
{
    const RoomProperty* const room = getroomprop(rx, ry);
    switch(room->tileset)
    {
    case 0: //Space Station
        switch(room->tilecol)
        {
        case 0:
            return 3;
            break;
        case 1:
            return 2;
            break;
        case 2:
            return 1;
            break;
        case 3:
            return 4;
            break;
        case 4:
            return 5;
            break;
        case 5:
            return 3;
            break;
        case 6:
            return 1;
            break;
        case 7:
            return 0;
            break;
        case 8:
            return 5;
            break;
        case 9:
            return 0;
            break;
        case 10:
            return 2;
            break;
        case 11:
            return 1;
            break;
        case 12:
            return 5;
            break;
        case 13:
            return 0;
            break;
        case 14:
            return 3;
            break;
        case 15:
            return 2;
            break;
        case 16:
            return 4;
            break;
        case 17:
            return 0;
            break;
        case 18:
            return 3;
            break;
        case 19:
            return 1;
            break;
        case 20:
            return 4;
            break;
        case 21:
            return 5;
            break;
        case 22:
            return 1;
            break;
        case 23:
            return 4;
            break;
        case 24:
            return 5;
            break;
        case 25:
            return 0;
            break;
        case 26:
            return 3;
            break;
        case 27:
            return 1;
            break;
        case 28:
            return 5;
            break;
        case 29:
            return 4;
            break;
        case 30:
            return 5;
            break;
        case 31:
            return 2;
            break;
        default:
            return 6;
            break;
        }
        break;
    case 1: //Outside
        switch(room->tilecol)
        {
        case 0:
            return 3;
            break;
        case 1:
            return 1;
            break;
        case 2:
            return 0;
            break;
        case 3:
            return 2;
            break;
        case 4:
            return 4;
            break;
        case 5:
            return 5;
            break;
        case 6:
            return 2;
            break;
        case 7:
            return 4;
            break;
        default:
            return 6;
            break;
        }
        break;
    case 2: //Lab
        switch(room->tilecol)
        {
        case 0:
            return 0;
            break;
        case 1:
            return 1;
            break;
        case 2:
            return 2;
            break;
        case 3:
            return 3;
            break;
        case 4:
            return 4;
            break;
        case 5:
            return 5;
            break;
        case 6:
            return 6;
            break;
        default:
            return 6;
            break;
        }
        break;
    case 3: //Warp Zone
        switch(room->tilecol)
        {
        case 0:
            return 0;
            break;
        case 1:
            return 1;
            break;
        case 2:
            return 2;
            break;
        case 3:
            return 3;
            break;
        case 4:
            return 4;
            break;
        case 5:
            return 5;
            break;
        case 6:
            return 6;
            break;
        default:
            return 6;
            break;
        }
        break;
    case 4: //Ship
        switch(room->tilecol)
        {
        case 0:
            return 5;
            break;
        case 1:
            return 0;
            break;
        case 2:
            return 4;
            break;
        case 3:
            return 2;
            break;
        case 4:
            return 3;
            break;
        case 5:
            return 1;
            break;
        case 6:
            return 6;
            break;
        default:
            return 6;
            break;
        }
        break;
    case 5: //Tower
        return 6;
        break;
    default:
        return 6;
        break;
    }
}

int customlevelclass::gettileidx(
    const int rx,
    const int ry,
    const int x,
    const int y
) {
    const int yoff = y + ry*30;
    int mult;
    int idx;

    if (Y_INBOUNDS(yoff))
    {
        mult = VMULT(yoff);
    }
    else
    {
        mult = 0;
    }

    idx = x + rx*40 + mult;

    return idx;
}

void customlevelclass::settile(
    const int rx,
    const int ry,
    const int x,
    const int y,
    const int t
) {
    const int idx = gettileidx(rx, ry, x, y);

    if (!INBOUNDS_ARR(idx, contents))
    {
        return;
    }

    contents[idx] = t;
}

int customlevelclass::gettile(
    const int rx,
    const int ry,
    const int x,
    const int y
) {
    const int idx = gettileidx(rx, ry, x, y);

    if (!INBOUNDS_ARR(idx, contents))
    {
        return 0;
    }

    return contents[idx];
}

int customlevelclass::getabstile(const int x, const int y)
{
    int idx;
    int yoff;

    if (Y_INBOUNDS(y))
    {
        yoff = VMULT(y);
    }
    else
    {
        yoff = 0;
    }

    idx = x + yoff;

    if (!INBOUNDS_ARR(idx, contents))
    {
        return 0;
    }

    return contents[idx];
}


int customlevelclass::getroompropidx(const int rx, const int ry)
{
    return rx + ry*maxwidth;
}

const RoomProperty* customlevelclass::getroomprop(const int rx, const int ry)
{
    const int idx = getroompropidx(rx, ry);

    if (INBOUNDS_ARR(idx, roomproperties))
    {
        return &roomproperties[idx];
    }

    static RoomProperty blank;
    blank.tileset = 1;
    blank.directmode = 1;
    blank.roomname.clear();

    return &blank;
}

#define FOREACH_PROP(NAME, TYPE) \
void customlevelclass::setroom##NAME(const int rx, const int ry, const TYPE NAME) \
{ \
    const int idx = getroompropidx(rx, ry); \
    \
    if (!INBOUNDS_ARR(idx, roomproperties)) \
    { \
        return; \
    } \
    \
    roomproperties[idx].NAME = NAME; \
}

ROOM_PROPERTIES

#undef FOREACH_PROP


int customlevelclass::absfree( int x, int y )
{
    //Returns 0 if tile is not a block, 1 otherwise, abs on grid
    if(x>=0 && y>=0 && x<mapwidth*40 && y<mapheight*30)
    {
        if(getabstile(x, y)==0)
        {
            return 0;
        }
        else
        {
            if(getabstile(x, y)>=2 && getabstile(x, y)<80)
            {
                return 0;
            }
            if(getabstile(x, y)>=680)
            {
                return 0;
            }
        }
    }
    return 1;
}

void customlevelclass::findstartpoint(void)
{
    //Ok! Scan the room for the closest checkpoint
    int testeditor=-1;
    //First up; is there a start point on this screen?
    for(size_t i=0; i<customentities.size(); i++)
    {
        //if() on screen
        if(customentities[i].t==16 && testeditor==-1)
        {
            testeditor=i;
        }
    }

    if(testeditor==-1)
    {
        game.edsavex = 160;
        game.edsavey = 120;
        game.edsaverx = 100;
        game.edsavery = 100;
        game.edsavegc = 0;
        game.edsavey--;
        game.edsavedir=1;
    }
    else
    {
        //Start point spawn
        int tx=customentities[testeditor].x/40;
        int ty=customentities[testeditor].y/30;
        game.edsavex = ((customentities[testeditor].x%40)*8)-4;
        game.edsavey = (customentities[testeditor].y%30)*8;
        game.edsaverx = 100+tx;
        game.edsavery = 100+ty;
        game.edsavegc = 0;
        game.edsavey++;
        game.edsavedir=1-customentities[testeditor].p1;
    }
}

int customlevelclass::findtrinket(int t)
{
    int ttrinket=0;
    for(int i=0; i<(int)customentities.size(); i++)
    {
        if(i==t) return ttrinket;
        if(customentities[i].t==9) ttrinket++;
    }
    return 0;
}

int customlevelclass::findcrewmate(int t)
{
    int ttrinket=0;
    for(int i=0; i<(int)customentities.size(); i++)
    {
        if(i==t) return ttrinket;
        if(customentities[i].t==15) ttrinket++;
    }
    return 0;
}

int customlevelclass::findwarptoken(int t)
{
    int ttrinket=0;
    for(int i=0; i<(int)customentities.size(); i++)
    {
        if(i==t) return ttrinket;
        if(customentities[i].t==13) ttrinket++;
    }
    return 0;
}


bool customlevelclass::load(std::string& _path)
{
    tinyxml2::XMLDocument doc;
    tinyxml2::XMLHandle hDoc(&doc);
    tinyxml2::XMLElement* pElem;

    reset();
#ifndef NO_EDITOR
    ed.reset();
#endif

    static const char *levelDir = "levels/";
    if (_path.compare(0, SDL_strlen(levelDir), levelDir) != 0)
    {
        _path = levelDir + _path;
    }

    FILESYSTEM_unmountAssets();
    if (game.cliplaytest && game.playassets != "")
    {
        MAYBE_FAIL(FILESYSTEM_mountAssets(game.playassets.c_str()));
    }
    else
    {
        MAYBE_FAIL(FILESYSTEM_mountAssets(_path.c_str()));
    }

    if (!FILESYSTEM_loadTiXml2Document(_path.c_str(), doc))
    {
        vlog_warn("%s not found", _path.c_str());
        goto fail;
    }

    if (doc.Error())
    {
        vlog_error("Error parsing %s: %s", _path.c_str(), doc.ErrorStr());
        goto fail;
    }

#ifndef NO_EDITOR
    ed.loaded_filepath = _path;
#endif

    version = 0;

    for (pElem = hDoc
        .FirstChildElement()
        .FirstChildElement("Data")
        .FirstChildElement()
        .ToElement();
    pElem != NULL;
    pElem = pElem->NextSiblingElement())
    {
        const char* pKey = pElem->Value();
        const char* pText = pElem->GetText();
        if(pText == NULL)
        {
            pText = "";
        }

        if (SDL_strcmp(pKey, "MetaData") == 0)
        {

            for( tinyxml2::XMLElement* subElem = pElem->FirstChildElement(); subElem; subElem= subElem->NextSiblingElement())
            {
                const char* pKey_ = subElem->Value();
                const char* pText_ = subElem->GetText() ;
                if(pText_ == NULL)
                {
                    pText_ = "";
                }

                if(SDL_strcmp(pKey_, "Creator") == 0)
                {
                    creator = pText_;
                }

                if(SDL_strcmp(pKey_, "Title") == 0)
                {
                    title = pText_;
                }

                if(SDL_strcmp(pKey_, "Desc1") == 0)
                {
                    Desc1 = pText_;
                }

                if(SDL_strcmp(pKey_, "Desc2") == 0)
                {
                    Desc2 = pText_;
                }

                if(SDL_strcmp(pKey_, "Desc3") == 0)
                {
                    Desc3 = pText_;
                }

                if(SDL_strcmp(pKey_, "website") == 0)
                {
                    website = pText_;
                }

                if(SDL_strcmp(pKey_, "onewaycol_override") == 0)
                {
                    onewaycol_override = help.Int(pText_);
                }
            }
        }

        if (SDL_strcmp(pKey, "mapwidth") == 0)
        {
            mapwidth = help.Int(pText);
        }
        if (SDL_strcmp(pKey, "mapheight") == 0)
        {
            mapheight = help.Int(pText);
        }
        if (SDL_strcmp(pKey, "levmusic") == 0)
        {
            levmusic = help.Int(pText);
        }


        if (SDL_strcmp(pKey, "contents") == 0 && pText[0] != '\0')
        {
            int x = 0;
            int y = 0;

            char buffer[16];
            size_t start = 0;

            while (next_split_s(buffer, sizeof(buffer), &start, pText, ','))
            {
                const int idx = x + maxwidth*40*y;

                if (INBOUNDS_ARR(idx, contents))
                {
                    contents[idx] = help.Int(buffer);
                }

                ++x;

                if (x == mapwidth*40)
                {
                    x = 0;
                    ++y;
                }
            }
        }


        if (SDL_strcmp(pKey, "edEntities") == 0)
        {
            for( tinyxml2::XMLElement* edEntityEl = pElem->FirstChildElement(); edEntityEl; edEntityEl=edEntityEl->NextSiblingElement())
            {
                CustomEntity entity = CustomEntity();
                const char* text = edEntityEl->GetText();

                if (text != NULL)
                {
                    size_t len = SDL_strlen(text);

                    // And now we come to the part where we have to deal with
                    // the terrible decisions of the past.
                    //
                    // For some reason, the closing tag of edentities generated
                    // by 2.2 and below has not only been put on a separate
                    // line, but also indented to match with the opening tag as
                    // well. Like this:
                    //
                    //    <edentity ...>contents
                    //    </edentity>
                    //
                    // Instead of doing <edentity ...>contents</edentity>.
                    //
                    // This is COMPLETELY terrible. This requires the XML to be
                    // parsed in an extremely specific and quirky way, which
                    // TinyXML-1 just happened to do.
                    //
                    // TinyXML-2 by default interprets the newline and the next
                    // indentation of whitespace literally, so you end up with
                    // tag contents that has a linefeed plus a bunch of extra
                    // spaces. You can't fix this by setting the whitespace
                    // mode to COLLAPSE_WHITESPACE, that does way more than
                    // TinyXML-1 ever did - it removes the leading whitespace
                    // from things like <edentity ...> this</edentity>, and
                    // collapses XML-encoded whitespace like <edentity ...>
                    // &#32; &#32;this</edentity>, which TinyXML-1 never did.
                    //
                    // Best solution here is to specifically hardcode removing
                    // the linefeed + the extremely specific amount of
                    // whitespace at the end of the contents.

                    if (endsWith(text, "\n            ")) // linefeed + exactly 12 spaces
                    {
                        // 12 spaces + 1 linefeed = 13 chars
                        len -= 13;
                    }

                    entity.scriptname = std::string(text, len);
                }
                edEntityEl->QueryIntAttribute("x", &entity.x);
                edEntityEl->QueryIntAttribute("y", &entity.y);
                edEntityEl->QueryIntAttribute("t", &entity.t);

                edEntityEl->QueryIntAttribute("p1", &entity.p1);
                edEntityEl->QueryIntAttribute("p2", &entity.p2);
                edEntityEl->QueryIntAttribute("p3", &entity.p3);
                edEntityEl->QueryIntAttribute("p4", &entity.p4);
                edEntityEl->QueryIntAttribute("p5", &entity.p5);
                edEntityEl->QueryIntAttribute("p6", &entity.p6);

                customentities.push_back(entity);
            }
        }

        if (SDL_strcmp(pKey, "levelMetaData") == 0)
        {
            int i = 0;
            for( tinyxml2::XMLElement* edLevelClassElement = pElem->FirstChildElement(); edLevelClassElement; edLevelClassElement=edLevelClassElement->NextSiblingElement())
            {
                if (!INBOUNDS_ARR(i, roomproperties))
                {
                    continue;
                }

                if(edLevelClassElement->GetText() != NULL)
                {
                    roomproperties[i].roomname = std::string(edLevelClassElement->GetText()) ;
                }

                edLevelClassElement->QueryIntAttribute("tileset", &roomproperties[i].tileset);
                edLevelClassElement->QueryIntAttribute("tilecol", &roomproperties[i].tilecol);
                edLevelClassElement->QueryIntAttribute("platx1", &roomproperties[i].platx1);
                edLevelClassElement->QueryIntAttribute("platy1", &roomproperties[i].platy1);
                edLevelClassElement->QueryIntAttribute("platx2", &roomproperties[i].platx2);
                edLevelClassElement->QueryIntAttribute("platy2", &roomproperties[i].platy2);
                edLevelClassElement->QueryIntAttribute("platv", &roomproperties[i].platv);
                edLevelClassElement->QueryIntAttribute("enemyx1", &roomproperties[i].enemyx1);
                edLevelClassElement->QueryIntAttribute("enemyy1", &roomproperties[i].enemyy1);
                edLevelClassElement->QueryIntAttribute("enemyx2", &roomproperties[i].enemyx2);
                edLevelClassElement->QueryIntAttribute("enemyy2", &roomproperties[i].enemyy2);
                edLevelClassElement->QueryIntAttribute("enemytype", &roomproperties[i].enemytype);
                edLevelClassElement->QueryIntAttribute("directmode", &roomproperties[i].directmode);

                edLevelClassElement->QueryIntAttribute("warpdir", &roomproperties[i].warpdir);

                i++;

            }
        }

        if (SDL_strcmp(pKey, "script") == 0 && pText[0] != '\0')
        {
            Script script_;
            bool headerfound = false;

            size_t start = 0;
            size_t len = 0;
            size_t prev_start = 0;

            while (next_split(&start, &len, &pText[start], '|'))
            {
                if (len > 0 && pText[prev_start + len - 1] == ':')
                {
                    if (headerfound)
                    {
                        script.customscripts.push_back(script_);
                    }

                    script_.name = std::string(&pText[prev_start], len - 1);
                    script_.contents.clear();
                    headerfound = true;

                    goto next;
                }

                if (headerfound)
                {
                    script_.contents.push_back(std::string(&pText[prev_start], len));
                }

next:
                prev_start = start;
            }

            /* Add the last script */
            if (headerfound)
            {
                script.customscripts.push_back(script_);
            }
        }
    }

    if (mapwidth < maxwidth)
    {
        /* Unscramble platv, since it was stored incorrectly
         * in 2.2 and previous... */
        size_t i;
        int x = 0;
        int y = 0;
        int temp_platv[numrooms];

        for (i = 0; i < numrooms; ++i)
        {
            temp_platv[i] = roomproperties[i].platv;
        }

        for (i = 0; i < numrooms; ++i)
        {
            if (x < mapwidth)
            {
                const int platv_idx = x + y * mapwidth;
                if (INBOUNDS_ARR(platv_idx, temp_platv))
                {
                    roomproperties[i].platv = temp_platv[platv_idx];
                }
            }
            else
            {
                roomproperties[i].platv = 4; /* default */
            }

            ++x;

            if (x >= maxwidth)
            {
                x = 0;
                ++y;
            }
        }
    }

#ifndef NO_EDITOR
    ed.gethooks();
#endif

    version=2;

    return true;

fail:
    return false;
}

#ifndef NO_EDITOR
bool customlevelclass::save(const std::string& _path)
{
    tinyxml2::XMLDocument doc;

    std::string newpath("levels/" + _path);

    // Try to preserve the XML of the currently-loaded one
    bool already_exists = !ed.loaded_filepath.empty()
    && FILESYSTEM_loadTiXml2Document(ed.loaded_filepath.c_str(), doc);
    if (!already_exists && !ed.loaded_filepath.empty())
    {
        vlog_error("Currently-loaded %s not found", ed.loaded_filepath.c_str());
    }

    ed.loaded_filepath = newpath;

    tinyxml2::XMLElement* msg;

    xml::update_declaration(doc);

    tinyxml2::XMLElement * root = xml::update_element(doc, "MapData");
    root->SetAttribute("version",version);

    xml::update_comment(root, " Save file ");

    tinyxml2::XMLElement * data = xml::update_element(root, "Data");

    msg = xml::update_element(data, "MetaData");

    //getUser
    xml::update_tag(msg, "Creator", creator.c_str());

    xml::update_tag(msg, "Title", title.c_str());

    xml::update_tag(msg, "Created", version);

    xml::update_tag(msg, "Modified", modifier.c_str());

    xml::update_tag(msg, "Modifiers", version);

    xml::update_tag(msg, "Desc1", Desc1.c_str());

    xml::update_tag(msg, "Desc2", Desc2.c_str());

    xml::update_tag(msg, "Desc3", Desc3.c_str());

    xml::update_tag(msg, "website", website.c_str());

    if (onewaycol_override)
    {
        xml::update_tag(msg, "onewaycol_override", onewaycol_override);
    }
    else
    {
        // Delete the element. I could just delete one, but just to be sure,
        // I will delete all of them if there are more than one
        tinyxml2::XMLElement* element;
        while ((element = msg->FirstChildElement("onewaycol_override"))
        != NULL)
        {
            doc.DeleteNode(element);
        }
    }

    xml::update_tag(data, "mapwidth", mapwidth);

    xml::update_tag(data, "mapheight", mapheight);

    xml::update_tag(data, "levmusic", levmusic);

    //New save format
    std::string contentsString="";
    for(int y = 0; y < mapheight*30; y++ )
    {
        for(int x = 0; x < mapwidth*40; x++ )
        {
            contentsString += help.String(getabstile(x, y)) + ",";
        }
    }
    xml::update_tag(data, "contents", contentsString.c_str());


    msg = xml::update_element_delete_contents(data, "edEntities");
    for(size_t i = 0; i < customentities.size(); i++)
    {
        tinyxml2::XMLElement *edentityElement = doc.NewElement( "edentity" );
        edentityElement->SetAttribute( "x", customentities[i].x);
        edentityElement->SetAttribute(  "y", customentities[i].y);
        edentityElement->SetAttribute(  "t", customentities[i].t);
        edentityElement->SetAttribute(  "p1", customentities[i].p1);
        edentityElement->SetAttribute(  "p2", customentities[i].p2);
        edentityElement->SetAttribute(  "p3", customentities[i].p3);
        edentityElement->SetAttribute( "p4", customentities[i].p4);
        edentityElement->SetAttribute( "p5", customentities[i].p5);
        edentityElement->SetAttribute(  "p6", customentities[i].p6);
        edentityElement->LinkEndChild( doc.NewText( customentities[i].scriptname.c_str() )) ;
        msg->LinkEndChild( edentityElement );
    }

    msg = xml::update_element_delete_contents(data, "levelMetaData");

    int temp_platv[numrooms];
    for (size_t i = 0; i < SDL_arraysize(temp_platv); ++i)
    {
        temp_platv[i] = 4; /* default */
    }

    if (mapwidth < maxwidth)
    {
        /* Re-scramble platv, since it was stored incorrectly
         * in 2.2 and previous... */
        size_t i;
        int x = 0;
        int y = 0;
        for (i = 0; i < numrooms; ++i)
        {
            if (x < mapwidth)
            {
                const int platv_idx = x + y * mapwidth;
                if (INBOUNDS_ARR(platv_idx, temp_platv))
                {
                    temp_platv[platv_idx] = roomproperties[i].platv;
                }
            }

            ++x;

            if (x >= mapwidth)
            {
                /* Skip to next actual row. */
                i += maxwidth - mapwidth;
                x = 0;
                ++y;
            }
        }
    }

    for(size_t i = 0; i < SDL_arraysize(roomproperties); i++)
    {
        tinyxml2::XMLElement *roompropertyElement = doc.NewElement( "edLevelClass" );
        roompropertyElement->SetAttribute( "tileset", roomproperties[i].tileset);
        roompropertyElement->SetAttribute(  "tilecol", roomproperties[i].tilecol);
        roompropertyElement->SetAttribute(  "platx1", roomproperties[i].platx1);
        roompropertyElement->SetAttribute(  "platy1", roomproperties[i].platy1);
        roompropertyElement->SetAttribute(  "platx2", roomproperties[i].platx2);
        roompropertyElement->SetAttribute( "platy2", roomproperties[i].platy2);
        roompropertyElement->SetAttribute( "platv", temp_platv[i]);
        roompropertyElement->SetAttribute(  "enemyx1", roomproperties[i].enemyx1);
        roompropertyElement->SetAttribute(  "enemyy1", roomproperties[i].enemyy1);
        roompropertyElement->SetAttribute(  "enemyx2", roomproperties[i].enemyx2);
        roompropertyElement->SetAttribute(  "enemyy2", roomproperties[i].enemyy2);
        roompropertyElement->SetAttribute(  "enemytype", roomproperties[i].enemytype);
        roompropertyElement->SetAttribute(  "directmode", roomproperties[i].directmode);
        roompropertyElement->SetAttribute(  "warpdir", roomproperties[i].warpdir);

        roompropertyElement->LinkEndChild( doc.NewText( roomproperties[i].roomname.c_str() )) ;
        msg->LinkEndChild( roompropertyElement );
    }

    std::string scriptString;
    for(size_t i = 0; i < script.customscripts.size(); i++)
    {
        Script& script_ = script.customscripts[i];

        scriptString += script_.name + ":|";
        for (size_t ii = 0; ii < script_.contents.size(); ++ii)
        {
            scriptString += script_.contents[ii];

            // Inserts a space if the line ends with a :
            if (script_.contents[ii].length() && *script_.contents[ii].rbegin() == ':')
            {
                scriptString += " ";
            }

            scriptString += "|";
        }
    }
    xml::update_tag(data, "script", scriptString.c_str());

    return FILESYSTEM_saveTiXml2Document(newpath.c_str(), doc);
}
#endif /* NO_EDITOR */


void customlevelclass::generatecustomminimap(void)
{
    map.customwidth=mapwidth;
    map.customheight=mapheight;

    map.customzoom=1;
    if(map.customwidth<=10 && map.customheight<=10) map.customzoom=2;
    if(map.customwidth<=5 && map.customheight<=5) map.customzoom=4;

    //Set minimap offsets
    if(map.customzoom==4)
    {
        map.custommmxoff=24*(5-map.customwidth);
        map.custommmxsize=240-(map.custommmxoff*2);

        map.custommmyoff=18*(5-map.customheight);
        map.custommmysize=180-(map.custommmyoff*2);
    }
    else if(map.customzoom==2)
    {
        map.custommmxoff=12*(10-map.customwidth);
        map.custommmxsize=240-(map.custommmxoff*2);

        map.custommmyoff=9*(10-map.customheight);
        map.custommmysize=180-(map.custommmyoff*2);
    }
    else
    {
        map.custommmxoff=6*(20-map.customwidth);
        map.custommmxsize=240-(map.custommmxoff*2);

        map.custommmyoff=int(4.5*(20-map.customheight));
        map.custommmysize=180-(map.custommmyoff*2);
    }

    FillRect(graphics.images[12], graphics.getRGB(0, 0, 0));

    int tm=0;
    int temp=0;
    //Scan over the map size
    if(mapheight<=5 && mapwidth<=5)
    {
        //4x map
        for(int j2=0; j2<mapheight; j2++)
        {
            for(int i2=0; i2<mapwidth; i2++)
            {
                //Ok, now scan over each square
                tm=196;
                if(getroomprop(i2, j2)->tileset==1) tm=96;

                for(int j=0; j<36; j++)
                {
                    for(int i=0; i<48; i++)
                    {
                        temp=absfree(int(i*0.83) + (i2*40),int(j*0.83)+(j2*30));
                        if(temp>=1)
                        {
                            //Fill in this pixel
                            FillRect(graphics.images[12], (i2*48)+i, (j2*36)+j, 1, 1, graphics.getRGB(tm, tm, tm));
                        }
                    }
                }
            }
        }
    }
    else if(mapheight<=10 && mapwidth<=10)
    {
        //2x map
        for(int j2=0; j2<mapheight; j2++)
        {
            for(int i2=0; i2<mapwidth; i2++)
            {
                //Ok, now scan over each square
                tm=196;
                if(getroomprop(i2, j2)->tileset==1) tm=96;

                for(int j=0; j<18; j++)
                {
                    for(int i=0; i<24; i++)
                    {
                        temp=absfree(int(i*1.6) + (i2*40),int(j*1.6)+(j2*30));
                        if(temp>=1)
                        {
                            //Fill in this pixel
                            FillRect(graphics.images[12], (i2*24)+i, (j2*18)+j, 1, 1, graphics.getRGB(tm, tm, tm));
                        }
                    }
                }
            }
        }
    }
    else
    {
        for(int j2=0; j2<mapheight; j2++)
        {
            for(int i2=0; i2<mapwidth; i2++)
            {
                //Ok, now scan over each square
                tm=196;
                if(getroomprop(i2, j2)->tileset==1) tm=96;

                for(int j=0; j<9; j++)
                {
                    for(int i=0; i<12; i++)
                    {
                        temp=absfree(3+(i*3) + (i2*40),(j*3)+(j2*30));
                        if(temp>=1)
                        {
                            //Fill in this pixel
                            FillRect(graphics.images[12], (i2*12)+i, (j2*9)+j, 1, 1, graphics.getRGB(tm, tm, tm));
                        }
                    }
                }
            }
        }
    }
}

// Return a graphics-ready color based off of the given tileset and tilecol
// Much kudos to Dav999 for saving me a lot of work, because I stole these colors from const.lua in Ved! -Info Teddy
Uint32 customlevelclass::getonewaycol(const int rx, const int ry)
{
    const RoomProperty* const room = getroomprop(rx, ry);
    switch (room->tileset) {

    case 0: // Space Station
        switch (room->tilecol) {
        case -1:
            return graphics.getRGB(109, 109, 109);
        case 0:
            return graphics.getRGB(131, 141, 235);
        case 1:
            return graphics.getRGB(227, 140, 227);
        case 2:
            return graphics.getRGB(242, 126, 151);
        case 3:
            return graphics.getRGB(229, 235, 133);
        case 4:
            return graphics.getRGB(148, 238, 130);
        case 5:
            return graphics.getRGB(140, 165, 227);
        case 6:
            return graphics.getRGB(227, 140, 148);
        case 7:
            return graphics.getRGB(140, 173, 228);
        case 8:
            return graphics.getRGB(142, 235, 137);
        case 9:
            return graphics.getRGB(137, 235, 206);
        case 10:
            return graphics.getRGB(235, 139, 223);
        case 11:
            return graphics.getRGB(238, 130, 138);
        case 12:
            return graphics.getRGB(137, 235, 178);
        case 13:
            return graphics.getRGB(125, 205, 247);
        case 14:
            return graphics.getRGB(190, 137, 235);
        case 15:
            return graphics.getRGB(235, 137, 206);
        case 16:
            return graphics.getRGB(229, 247, 127);
        case 17:
            return graphics.getRGB(127, 200, 247);
        case 18:
            return graphics.getRGB(197, 137, 235);
        case 19:
            return graphics.getRGB(235, 131, 175);
        case 20:
            return graphics.getRGB(242, 210, 123);
        case 21:
            return graphics.getRGB(131, 235, 158);
        case 22:
            return graphics.getRGB(242, 126, 151);
        case 23:
            return graphics.getRGB(219, 243, 123);
        case 24:
            return graphics.getRGB(131, 234, 145);
        case 25:
            return graphics.getRGB(131, 199, 234);
        case 26:
            return graphics.getRGB(141, 131, 234);
        case 27:
            return graphics.getRGB(226, 140, 144);
        case 28:
            return graphics.getRGB(129, 236, 144);
        case 29:
            return graphics.getRGB(235, 231, 131);
        case 30:
            return graphics.getRGB(153, 235, 131);
        case 31:
            return graphics.getRGB(207, 131, 235);
        }
        break;

    case 1: // Outside
        switch (room->tilecol) {
        case 0:
            return graphics.getRGB(57, 86, 140);
        case 1:
            return graphics.getRGB(156, 42, 42);
        case 2:
            return graphics.getRGB(42, 156, 155);
        case 3:
            return graphics.getRGB(125, 36, 162);
        case 4:
            return graphics.getRGB(191, 198, 0);
        case 5:
            return graphics.getRGB(0, 198, 126);
        case 6:
            return graphics.getRGB(224, 110, 177);
        case 7:
            return graphics.getRGB(255, 142, 87);
        }
        break;

    case 2: // Lab
        switch (room->tilecol) {
        case 0:
            return graphics.getRGB(0, 165, 206);
        case 1:
            return graphics.getRGB(206, 5, 0);
        case 2:
            return graphics.getRGB(222, 0, 173);
        case 3:
            return graphics.getRGB(27, 67, 255);
        case 4:
            return graphics.getRGB(194, 206, 0);
        case 5:
            return graphics.getRGB(0, 206, 39);
        case 6:
            return graphics.getRGB(0, 165, 206);
        }
        break;

    case 3: // Warp Zone
        switch (room->tilecol) {
        case 0:
            return graphics.getRGB(113, 178, 197);
        case 1:
            return graphics.getRGB(197, 113, 119);
        case 2:
            return graphics.getRGB(196, 113, 197);
        case 3:
            return graphics.getRGB(149, 113, 197);
        case 4:
            return graphics.getRGB(197, 182, 113);
        case 5:
            return graphics.getRGB(141, 197, 113);
        case 6:
            return graphics.getRGB(109, 109, 109);
        }
        break;

    case 4: // Ship
        switch (room->tilecol) {
        case 0:
            return graphics.getRGB(0, 206, 39);
        case 1:
            return graphics.getRGB(0, 165, 206);
        case 2:
            return graphics.getRGB(194, 206, 0);
        case 3:
            return graphics.getRGB(206, 0, 160);
        case 4:
            return graphics.getRGB(27, 67, 255);
        case 5:
            return graphics.getRGB(206, 5, 0);
        }
        break;

    }

    // Uh, I guess return solid white
    return graphics.getRGB(255, 255, 255);
}

// This version detects the room automatically
Uint32 customlevelclass::getonewaycol(void)
{
#ifndef NO_EDITOR
    if (game.gamestate == EDITORMODE)
    {
        return getonewaycol(ed.levx, ed.levy);
    }
    else
#endif
    if (map.custommode)
    {
        return getonewaycol(game.roomx - 100, game.roomy - 100);
    }

    // Uh, I guess return solid white
    return graphics.getRGB(255, 255, 255);
}

static SDL_INLINE bool inbounds(const CustomEntity* entity)
{
    extern customlevelclass cl;
    return entity->x >= 0
    && entity->y >= 0
    && entity->x < cl.mapwidth * SCREEN_WIDTH_TILES
    && entity->y < cl.mapheight * SCREEN_HEIGHT_TILES;
}

int customlevelclass::numtrinkets(void)
{
    int temp = 0;
    for (size_t i = 0; i < customentities.size(); i++)
    {
        if (customentities[i].t == 9 && inbounds(&customentities[i]))
        {
            temp++;
        }
    }
    return temp;
}

int customlevelclass::numcrewmates(void)
{
    int temp = 0;
    for (size_t i = 0; i < customentities.size(); i++)
    {
        if (customentities[i].t == 15 && inbounds(&customentities[i]))
        {
            temp++;
        }
    }
    return temp;
}

#endif /* NO_CUSTOM_LEVELS */
