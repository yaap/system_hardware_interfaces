/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
///////////////////////////////////////////////////////////////////////////////
// THIS FILE IS IMMUTABLE. DO NOT EDIT IN ANY CASE.                          //
///////////////////////////////////////////////////////////////////////////////

// This file is a snapshot of an AIDL file. Do not edit it manually. There are
// two cases:
// 1). this is a frozen version file - do not edit this in any case.
// 2). this is a 'current' file. If you make a backwards compatible change to
//     the interface (from the latest frozen version), the build system will
//     prompt you to update this file with `m <name>-update-api`.
//
// You must not make a backward incompatible change to any AIDL file built
// with the aidl_interface module type with versions property set. The module
// type is used to build AIDL files in a way that they can be used across
// independently updatable components of the system. If a device is shipped
// with such a backward incompatible change, it has a high risk of breaking
// later when a module using the interface is updated, e.g., Mainline modules.

package android.system.keystore2;
/* @hide */
@VintfStability
interface IKeystoreService {
  android.system.keystore2.IKeystoreSecurityLevel getSecurityLevel(in android.hardware.security.keymint.SecurityLevel securityLevel);
  android.system.keystore2.KeyEntryResponse getKeyEntry(in android.system.keystore2.KeyDescriptor key);
  void updateSubcomponent(in android.system.keystore2.KeyDescriptor key, in @nullable byte[] publicCert, in @nullable byte[] certificateChain);
  /**
   * @deprecated use listEntriesBatched instead.
   */
  android.system.keystore2.KeyDescriptor[] listEntries(in android.system.keystore2.Domain domain, in long nspace);
  void deleteKey(in android.system.keystore2.KeyDescriptor key);
  android.system.keystore2.KeyDescriptor grant(in android.system.keystore2.KeyDescriptor key, in int granteeUid, in int accessVector);
  void ungrant(in android.system.keystore2.KeyDescriptor key, in int granteeUid);
  int getNumberOfEntries(in android.system.keystore2.Domain domain, in long nspace);
  android.system.keystore2.KeyDescriptor[] listEntriesBatched(in android.system.keystore2.Domain domain, in long nspace, in @nullable String startingPastAlias);
}
