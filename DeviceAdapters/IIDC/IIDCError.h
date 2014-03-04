// Micro-Manager IIDC Device Adapter
//
// AUTHOR:        Mark A. Tsuchida
//
// COPYRIGHT:     University of California, San Francisco, 2014
//
// LICENSE:       This file is distributed under the BSD license.
//                License text is included with the source distribution.
//
//                This file is distributed in the hope that it will be useful,
//                but WITHOUT ANY WARRANTY; without even the implied warranty
//                of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//
//                IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//                CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
//                INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES.

#pragma once

#include <dc1394/dc1394.h>

#include <boost/lexical_cast.hpp>
#include <exception>
#include <string>


namespace IIDC {

class Error : public std::exception {
   std::string msg_;

public:
   Error(dc1394error_t err, const std::string& msg)
   {
      if (err != DC1394_SUCCESS)
         msg_ = msg + " [libdc1394: " + dc1394_error_get_string(err) + " (" +
            boost::lexical_cast<std::string>(err) + ")]";
      else
         msg_ = msg;
   }

   explicit Error(const std::string& msg) : msg_(msg) {}
   virtual ~Error() throw () {}
   virtual const char* what() const throw () { return msg_.c_str(); }
};

} // namespace IIDC
