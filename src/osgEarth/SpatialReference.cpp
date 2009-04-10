/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
 * Copyright 2008-2009 Pelican Ventures, Inc.
 * http://osgearth.org
 *
 * osgEarth is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include <osgEarth/SpatialReference>
#include <osgEarth/Registry>
#include <OpenThreads/ScopedLock>
#include <osg/Notify>
#include <ogr_api.h>
#include <ogr_spatialref.h>
#include <algorithm>

using namespace osgEarth;


#define WKT_


#define OGR_SCOPE_LOCK() \
    OpenThreads::ScopedLock<OpenThreads::ReentrantMutex> _slock( osgEarth::Registry::instance()->getOGRMutex() )


SpatialReference*
SpatialReference::createFromPROJ4( const std::string& init, const std::string& init_alias )
{
    SpatialReference* result = NULL;
    OGR_SCOPE_LOCK();
	void* handle = OSRNewSpatialReference( NULL );
    if ( OSRImportFromProj4( handle, init.c_str() ) == OGRERR_NONE )
	{
        result = new SpatialReference( handle, "PROJ4", init_alias );
	}
	else 
	{
		osg::notify(osg::WARN) << "Unable to create spatial reference from PROJ4: " << init << std::endl;
		OSRDestroySpatialReference( handle );
	}
    return result;
}

SpatialReference*
SpatialReference::createFromWKT( const std::string& init, const std::string& init_alias )
{
    SpatialReference* result = NULL;
    OGR_SCOPE_LOCK();
	void* handle = OSRNewSpatialReference( NULL );
    char buf[4096];
    char* buf_ptr = &buf[0];
	strcpy( buf, init.c_str() );
	if ( OSRImportFromWkt( handle, &buf_ptr ) == OGRERR_NONE )
	{
        result = new SpatialReference( handle, "WKT", init_alias );
	}
	else 
	{
		osg::notify(osg::WARN) << "Unable to create spatial reference from WKT: " << init << std::endl;
		OSRDestroySpatialReference( handle );
	}
    return result;
}

SpatialReference*
SpatialReference::create( const std::string& init )
{
    std::string low = init;
    std::transform( low.begin(), low.end(), low.begin(), ::tolower );

    // shortcut some well-known codes:
    if (low == "epsg:900913" || low == "epsg:3785" || low == "epsg:41001" ||
        low == "epsg:54004" || low == "epsg:9804" || low == "epsg:9805")
    {
        return createFromPROJ4(
            "+proj=merc +lon_0=0 +k=1 +x_0=0 +y_0=0 +ellps=WGS84 +datum=WGS84 +units=m +no_defs", init );
    }

    if (low == "epsg:4326" || low == "wgs84")
    {
        return createFromPROJ4( "+proj=longlat +ellps=WGS84 +datum=WGS84 +no_defs", init );
    }

    if ( low.find( "+" ) == 0 )
        return createFromPROJ4( low, init );

    if ( low.find( "epsg:" ) == 0 || low.find( "osgeo:" ) == 0 )
        return createFromPROJ4( "+init=" + low, init );

    if ( low.find( "projcs" ) == 0 || low.find( "geogcs" ) == 0 )
        return createFromWKT( init, init );

    else
        return NULL;
}

/****************************************************************************/


SpatialReference::SpatialReference(void* handle, 
                                   const std::string& init_type,
                                   const std::string& init_str) :
_handle( handle ),
_init_type( init_type ),
_init_str( init_str ),
_owns_handle( true ),
_initialized( false )
{
    //TODO
    _init_str_lc = init_str;
    std::transform( _init_str_lc.begin(), _init_str_lc.end(), _init_str_lc.begin(), ::tolower );
}

SpatialReference::~SpatialReference()
{
	if ( _handle && _owns_handle )
	{
      OGR_SCOPE_LOCK();
      OSRDestroySpatialReference( _handle );
	}
	_handle = NULL;
}

bool
SpatialReference::isGeographic() const 
{
    if ( !_initialized )
        const_cast<SpatialReference*>(this)->init();
    return _is_geographic;
}

bool
SpatialReference::isProjected() const
{
    if ( !_initialized )
        const_cast<SpatialReference*>(this)->init();
    return !_is_geographic;
}

const std::string&
SpatialReference::getName() const
{
    if ( !_initialized )
        const_cast<SpatialReference*>(this)->init();
    return _name;
}

const osg::EllipsoidModel*
SpatialReference::getEllipsoid() const
{
    if ( !_initialized )
        const_cast<SpatialReference*>(this)->init();
    return _ellipsoid.get();
}

const std::string&
SpatialReference::getWKT() const 
{
    if ( !_initialized )
        const_cast<SpatialReference*>(this)->init();
    return _wkt;
}

const std::string&
SpatialReference::getInitString() const
{
    return _init_str;
}

const std::string&
SpatialReference::getInitType() const
{
    return _init_type;
}

bool
SpatialReference::isEquivalentTo( const SpatialReference* rhs ) const
{
    if ( !_initialized )
        const_cast<SpatialReference*>(this)->init();

    if ( !rhs )
        return false;

    if ( this == rhs )
        return true;

    if ( _init_str_lc == rhs->_init_str_lc )
        return true;

    if ( this->getWKT() == rhs->getWKT() )
        return true;

    if (this->isGeographic() && rhs->isGeographic() &&
        this->getEllipsoid()->getRadiusEquator() == rhs->getEllipsoid()->getRadiusEquator() &&
        this->getEllipsoid()->getRadiusPolar() == rhs->getEllipsoid()->getRadiusPolar())
    {
        return true;
    }

    // last resort, since it requires the lock
    OGR_SCOPE_LOCK();
    return TRUE == ::OSRIsSame( _handle, rhs->_handle );
}

bool
SpatialReference::isMercator() const
{
    if ( !_initialized )
        const_cast<SpatialReference*>(this)->init();
    return _is_mercator;
}


bool
SpatialReference::transform( double x, double y, const SpatialReference* out_srs, double& out_x, double& out_y ) const
{        
    //TODO: should we check for equivalence here, or leave that to the caller?
    // (Or, does OGR do the test under the hood itself?)

    OGR_SCOPE_LOCK();

    void* xform_handle = OCTNewCoordinateTransformation( _handle, out_srs->_handle );
    if ( !xform_handle )
    {
        osg::notify( osg::WARN )
            << "[osgEarth::SpatialReference] SRS xform not possible" << std::endl
            << "    From => " << getName() << std::endl
            << "    To   => " << out_srs->getName() << std::endl;
        return false;
    }

    double temp_x = x;
    double temp_y = y;
    double temp_z = 0.0;
    bool result;

    if ( OCTTransform( xform_handle, 1, &temp_x, &temp_y, &temp_z ) )
    {
        result = true;
        out_x = temp_x;
        out_y = temp_y;
    }
    else
    {
        osg::notify( osg::WARN ) << "[osgEarth::SpatialReference] Failed to xform a point from "
            << getName() << " to " << out_srs->getName()
            << std::endl;
        result = false;
    }

    OCTDestroyCoordinateTransformation( xform_handle );
    return result;
}

static std::string
getOGRAttrValue( void* _handle, const std::string& name, int child_num, bool lowercase =false)
{
    OGR_SCOPE_LOCK();
	const char* val = OSRGetAttrValue( _handle, name.c_str(), child_num );
    if ( val )
    {
        std::string t = val;
        if ( lowercase )
        {
            std::transform( t.begin(), t.end(), t.begin(), ::tolower );
        }
        return t;
    }
    return "";
}

void
SpatialReference::init()
{
    OGR_SCOPE_LOCK();
    
    _is_geographic = OSRIsGeographic( _handle ) != 0;
    
    int err;
    double semi_major_axis = OSRGetSemiMajor( _handle, &err );
    double semi_minor_axis = OSRGetSemiMinor( _handle, &err );
    _ellipsoid = new osg::EllipsoidModel( semi_major_axis, semi_minor_axis );

    _name = _is_geographic? 
        getOGRAttrValue( _handle, "GEOGCS", 0 ) : 
        getOGRAttrValue( _handle, "PROJCS", 0 );

    std::string proj = getOGRAttrValue( _handle, "PROJECTION", 0, true );
    _is_mercator = !proj.empty() && proj.find("mercator")==0;

    char* wktbuf;
    if ( OSRExportToWkt( _handle, &wktbuf ) == OGRERR_NONE )
    {
        _wkt = wktbuf;
        OGRFree( wktbuf );
    }

    _initialized = true;
}