
#include <assert.h>
#include <errno.h>
#include <locale.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <OpenBcd.h>

#if _WIN32

#define WIN32_LEAN_AND_MEAN 1
#include <io.h>
#include <Windows.h>

#elif __unix__

#include <unistd.h>

#endif

// TODO: Handle 32-bit wchar_t

#define arrlen_literal(X) (sizeof(X) / sizeof(*(X)))
#define strlen_literal(X) (arrlen_literal(X) - 1)

static uint8_t StoreSaveHandler(void* pBuffer, size_t nBuffer, void* Parameter) {
	FILE* File = Parameter;
#if _WIN32
	(void)_chsize(_fileno(File), (long)nBuffer);
#elif __unix__
	ftruncate(fileno(File), (off_t)nBuffer);
#endif
	rewind(File);
	return (fwrite(pBuffer, nBuffer, 1, File) == 1);
}

int real_wmain(int argc, wchar_t** argv);

#ifdef _WIN32

int wmain(int argc, wchar_t** argv) {
	// At least for UCRT, stdio does not respect the code page set by chcp
	// so we need to manually set it. Not sure how this interacts with MSVCRT.
	// One bad thing is the input code page (GetConsoleCP()) is ignored as
	// setlocale() doesn't differentiate between these 2 cases. C++ does better here.
	// A workaround would be _configthreadlocale() then setlocale() but that's for the future.

	char sLocale[arrlen_literal(".4294967295")];
	snprintf(sLocale, arrlen_literal(sLocale), ".%"PRIu32, GetConsoleOutputCP());
	if (setlocale(LC_ALL, sLocale) == NULL)
		fprintf(stderr, "Warning: Unable to match CRT multi-byte code page with console.\n");

	return real_wmain(argc, argv);
}

#else

int main(int argc, char** argv) {
	// Convert multi-byte to wide string.
	setlocale(LC_ALL, "");

	wchar_t** wargv = malloc(argc * sizeof(*wargv));
	if (wargv == NULL) {
		fprintf(stderr, "Error: Memory allocation failed for argv.\n");
		return -1;
	}

	int i;
	for (i = 0; i < argc; ++i) {
		mbstate_t MbState;
		memset(&MbState, 0, sizeof(MbState));

		const char* sSource = argv[i];
		const char** restrict psSource = &sSource;
		size_t WideLength = mbsrtowcs(NULL, psSource, 0, &MbState); // Without '\0'
		if (WideLength == SIZE_MAX) {
			fprintf(stderr, "Error: mbsrtowcs failed for argv[%i].\n", i);
			break;
		}

		wargv[i] = malloc((WideLength + 1) * sizeof(wargv[i][0]));
		if (wargv[i] == NULL) {
			fprintf(stderr, "Error: Memory allocation failed for argv[%i].\n", i);
			break;
		}

		sSource = argv[i];
		size_t WideLength2 = mbsrtowcs(wargv[i], psSource, WideLength, &MbState);
		assert(WideLength2 == WideLength);
		wargv[i][WideLength] = L'\0';
	}

	int Result;
	if (i == argc) {
		// Conversion succeed
		Result = real_wmain(argc, wargv);
	} else {
		Result = -1;
	}

	for (int ii = 0; ii < i; ++ii)
		free(wargv[ii]);

	free(wargv);
	return Result;
}

#endif

int wfopen_s_wrapper(FILE** pFile, const wchar_t* sFileName, const wchar_t* sMode) {
#ifdef _WIN32

	return _wfopen_s(pFile, sFileName, sMode);

#else

	mbstate_t MbState;
	memset(&MbState, 0, sizeof(MbState));
	const wchar_t* sSource;
	const wchar_t** restrict psSource = &sSource;
	
	sSource = sFileName;
	size_t MultiByteFileNameLength =
		wcsrtombs(NULL, psSource, 0, &MbState); // Without '\0'
	if (MultiByteFileNameLength == SIZE_MAX)
		return EILSEQ;

	sSource = sMode;
	size_t MultiByteModeLength =
		wcsrtombs(NULL, psSource, 0, &MbState); // Without '\0'
	if (MultiByteModeLength == SIZE_MAX)
		return EILSEQ;

	char* sMultiByteFileName =
		malloc((MultiByteFileNameLength + 1) * sizeof(*sMultiByteFileName));
	if (sMultiByteFileName == NULL)
		return ENOMEM;

	char* sMultiByteMode =
		malloc((MultiByteModeLength + 1) * sizeof(*sMultiByteMode));
	if (sMultiByteMode == NULL) {
		free(sMultiByteFileName);
		return ENOMEM;
	}

	sSource = sFileName;
	size_t MultiByteFileNameLength2 =
		wcsrtombs(sMultiByteFileName, psSource, MultiByteFileNameLength, &MbState);
	assert(MultiByteFileNameLength2 == MultiByteFileNameLength);
	sMultiByteFileName[MultiByteFileNameLength] = L'\0';

	sSource = sMode;
	size_t MultiByteModeLength2 =
		wcsrtombs(sMultiByteMode, psSource, MultiByteModeLength, &MbState);
	assert(MultiByteModeLength2 == MultiByteModeLength);
	sMultiByteMode[MultiByteModeLength] = L'\0';

	FILE* File = fopen(sMultiByteFileName, sMultiByteMode);
	int Result;
	if (File == NULL) {
		Result = errno;
	} else {
		*pFile = File;
		Result = 0;
	}

	free(sMultiByteMode);
	free(sMultiByteFileName);
	return Result;

#endif
}

int real_wmain(int argc, wchar_t** argv) {
	if (argc < 2)
		return -1;

	FILE* StoreFile;
	int Error = wfopen_s_wrapper(&StoreFile, argv[1], L"rb+");
	if (Error != 0) {
		fprintf(stderr, "Error opening the store file. errno: %i\n", Error);
		return -1;
	}

	int Result = 0;
	if (fseek(StoreFile, 0, SEEK_END)) {
		fprintf(stderr, "Error seeking the store file.\n");
		Result = -1;
		goto CleanupStoreFile;
	}
	long StoreFileSizeL = ftell(StoreFile);
	if (StoreFileSizeL < 0) {
		fprintf(stderr, "Error getting the store file's position.\n");
		Result = -1;
		goto CleanupStoreFile;
	}
	size_t StoreFileSize = (size_t)StoreFileSizeL;
	rewind(StoreFile);

	void* pStoreBuffer = malloc(StoreFileSize);
	if (pStoreBuffer == NULL) {
		fprintf(stderr, "Error allocating memory for the store file.\n");
		Result = -1;
		goto CleanupStoreFile;
	}
	if (fread(pStoreBuffer, StoreFileSize, 1, StoreFile) != 1) {
		fprintf(stderr, "Error reading the store file.\n");
		Result = -1;
		goto CleanupStoreBuffer;
	}

	h_bcd_store hStore;
	bcd_status Status;
	Status = Bcd_LoadStore(
		&hStore,
		pStoreBuffer,
		StoreFileSize,
		StoreSaveHandler,
		StoreFile
	);
	if (Status) {
		fprintf(stderr, "Error loading the store file.\n");
		Result = (int)Status;
		goto CleanupStoreBuffer;
	}

	REGISTRY_STRING_DECLARE_FIXED(
		aDefaultObjectGuidString,
		DefaultObjectGuidString,
		BCD_GUID_STRING_LENGTH
	);
	Bcd_QueryDefaultObjectGuidString(hStore, &DefaultObjectGuidString);

	bcd_object_iterator iObject;
	Status = Bcd_OpenObjectIterator(hStore, &iObject);
	if (Status) {
		fprintf(stderr, "Error opening the object iterator.\n");
		Result = (int)Status;
		goto CleanupStore;
	}

	while (1) {
		h_bcd_object hObject;
		Status = Bcd_OpenNextObject(&iObject, &hObject);
		if (Status) {
			if (Status == BCD_STATUS_NO_MORE_ENTRIES)
				break;
			Result = (int)Status;
			goto CleanupObjectIterator;
		}

		REGISTRY_STRING_DECLARE_FIXED(aGuidString, GuidString, BCD_GUID_STRING_LENGTH);
		Status = Bcd_QueryObjectGuidString(hObject, &GuidString);
		if (Status) {
			fprintf(stderr, "Error querying the object's GUID string.\n");
			Result = (int)Status;
			goto CleanupObject;
		}

		const registry_string16 EmptyString = {0};
		registry_string16 ObjectFriendlyName =
			Bcd_GetObjectFriendlyName(GuidString, DefaultObjectGuidString, EmptyString);
		if (ObjectFriendlyName.aBuffer == NULL)
			ObjectFriendlyName = GuidString;

		wchar_t aWideGuid[BCD_GUID_STRING_LENGTH];
		size_t nWideGuid = Registry_ConvertString16ToWideString(
			ObjectFriendlyName,
			aWideGuid,
			arrlen_literal(aWideGuid)
		);

		bcd_object_type ObjectType;
		Status = Bcd_QueryObjectType(hObject, &ObjectType);
		if (Status) {
			fprintf(stderr, "Error querying object type.\n");
			Result = (int)Status;
			goto CleanupObject;
		}

		registry_string16 Description = Bcd_GetObjectDescription(ObjectType);

		REGISTRY_STRING_DECLARE_ASCII_LITERAL(
			aUnknownDescription,
			UnknownDescription,
			L"Unknown object"
		);
		// TODO: Remove this branch

		if (Description.aBuffer == NULL)
			Description = UnknownDescription;

		wchar_t aWideDescription[64];
		size_t nWideDescription = Registry_ConvertString16ToWideStringAscii(
			Description,
			aWideDescription,
			arrlen_literal(aWideDescription)
		);

		// Using non-wide printf should on theory works the same way and should
		// save us some bytes but on Windows it's more buggy, so use wprintf
		// whenever we want to print wide string.
		wprintf(L"%.*ls\n", (int)nWideDescription, aWideDescription);
		for (size_t i = 0; i < nWideDescription; ++i)
			putchar('-');
		putchar('\n');
		wprintf(L"%-24ls%.*ls\n", L"identifier", (int)nWideGuid, aWideGuid);

		bcd_element_iterator iElement;
		Bcd_OpenElementIterator(hObject, &iElement);
		while (1) {
			h_bcd_element hElement;
			Status = Bcd_OpenNextElement(&iElement, &hElement);
			if (Status) {
				if (Status == BCD_STATUS_NO_MORE_ENTRIES)
					break;
				fprintf(stderr, "Error opening the next element.\n");
				Result = (int)Status;
				goto CleanupElementIterator;
			}

			bcd_element_id ElementId;
			Status = Bcd_QueryElementId(hElement, &ElementId);
			if (Status) {
				fprintf(stderr, "Error querying element ID.\n");
				Result = (int)Status;
				goto CleanupElement;
			}

			registry_string16 ElementFriendlyName;
			Status = Bcd_QueryElementFriendlyName(hElement, &ElementFriendlyName);
			if (Status) {
				fprintf(stderr, "Error querying element friendly name.\n");
				Result = (int)Status;
				goto CleanupElement;
			}

			static const wchar_t sCustom[] = L"custom:";
			uint16_t aCustom[strlen_literal(sCustom) + BCD_ELEMENT_ID_STRING_LENGTH];
			
			// Use custom:ElementId if friendly name is not available
			if (ElementFriendlyName.aBuffer == NULL) {
				ElementFriendlyName.aBuffer = aCustom;
				ElementFriendlyName.AllocatedLength = arrlen_literal(aCustom);

				Registry_ConvertWideStringToString16Ascii(
					sCustom,
					strlen_literal(sCustom),
					&ElementFriendlyName
				);

				ElementFriendlyName.aBuffer = &aCustom[strlen_literal(sCustom)];
				Bcd_ElementIdToString(ElementId, &ElementFriendlyName);
				ElementFriendlyName.aBuffer = aCustom;
				ElementFriendlyName.Length = strlen_literal(sCustom) + BCD_ELEMENT_ID_STRING_LENGTH;
			}

			// Display friendly name
			wchar_t aWideElementName[64];
			size_t nWideElementName = Registry_ConvertString16ToWideStringAscii(
				ElementFriendlyName,
				aWideElementName,
				arrlen_literal(aWideElementName)
			);
			wprintf(L"%-24.*ls", (int)nWideElementName, aWideElementName);
			
			uint8_t aBuffer[512];
			uint32_t nBuffer;
			Status = Bcd_QueryElementValue(hElement, aBuffer, sizeof(aBuffer), &nBuffer);
			if (Status) {
				fprintf(stderr, "Error querying element value.\n");
				Result = (int)Status;
				goto CleanupElement;
			}
			// Ignore big element value for now

			// Display element data

			bcd_element_type ElementType = Bcd_ElementIdToElementType(ElementId);
			wchar_t aWideElementData[256];
			size_t nWideElementData = arrlen_literal(aWideElementData);
			size_t nWritten;
			uint8_t bFirstLine = 1;
			switch (ElementType) {
				case Bcd_ElementType_Device:
					printf("unimplemented\n");
					break;

				case Bcd_ElementType_String: {
					registry_string16 ElementString = {
						nBuffer / sizeof(uint16_t) - 1,
						nBuffer / sizeof(uint16_t) - 1,
						(uint16_t*)aBuffer
					};
					Registry_FixString16(ElementString, ElementString.aBuffer);
					nWritten = Registry_ConvertString16ToWideString(
						ElementString,
						aWideElementData,
						nWideElementData
					);

					wprintf(L"%.*ls\n", (int)nWritten, aWideElementData);
					break;
				}

				case Bcd_ElementType_Guid:
					GuidString = (registry_string16){
						nBuffer / sizeof(uint16_t) - 1,
						nBuffer / sizeof(uint16_t) - 1,
						(uint16_t*)aBuffer
					};
					ObjectFriendlyName =
						Bcd_GetObjectFriendlyName(
							GuidString,
							DefaultObjectGuidString,
							EmptyString
						);
					if (ObjectFriendlyName.aBuffer == NULL)
						ObjectFriendlyName = GuidString;

					nWritten = Registry_ConvertString16ToWideStringAscii(
						ObjectFriendlyName,
						aWideElementData,
						nWideElementData
					);

					wprintf(L"%.*ls\n", (int)nWritten, aWideElementData);
					break;

				case Bcd_ElementType_GuidList: {
					uint16_t* ss = (uint16_t*)aBuffer;
					size_t n = nBuffer / sizeof(wchar_t);
					size_t iOffset = 0;

					while (iOffset < n && ss[iOffset] != '\0') {
						uint16_t* s = &ss[iOffset];
						size_t Length = Registry_CountString16(s);

						GuidString = (registry_string16){Length, Length, s};
						ObjectFriendlyName =
							Bcd_GetObjectFriendlyName(
								GuidString,
								DefaultObjectGuidString,
								EmptyString
							);
						if (ObjectFriendlyName.aBuffer == NULL)
							ObjectFriendlyName = GuidString;

						nWritten = Registry_ConvertString16ToWideStringAscii(
							ObjectFriendlyName,
							aWideElementData,
							nWideElementData
						);
						if (!bFirstLine) // Padding
							printf("%-24s", "");
						wprintf(L"%.*ls\n", (int)nWritten, aWideElementData);

						iOffset += Length + 1;
						bFirstLine = 0;
					}
					break;
				}

				case Bcd_ElementType_Integer:
					printf("%"PRIu64"\n", *(uint64_t*)aBuffer);
					break;

				case Bcd_ElementType_Boolean:
					printf("%s\n", (*(uint8_t*)aBuffer) ? "Yes" : "No");
					break;

				case Bcd_ElementType_IntegerList: {
					uint64_t* aInteger = (uint64_t*)aBuffer;
					size_t IntegerCount = nBuffer / sizeof(uint64_t);

					for (size_t i = 0; i < IntegerCount; ++i) {
						if (!bFirstLine) // Padding
							printf("%-24s", "");
						printf("0x%"PRIx64"\n", aInteger[i]);
						bFirstLine = 0;
					}
					break;
				}
			}
			
			Bcd_CloseElement(hElement);
			continue;

			CleanupElement:
			Bcd_CloseElement(hElement);
		}
		putchar('\n');

		Bcd_CloseObject(hObject);
		continue;

		CleanupElementIterator:
		Bcd_CloseElementIterator(iElement);

		CleanupObject:
		Bcd_CloseObject(hObject);
		break;
	}

	CleanupObjectIterator:
	Bcd_CloseObjectIterator(iObject);

	CleanupStore:
	Bcd_UnloadStore(hStore, 1);

	CleanupStoreBuffer:
	free(pStoreBuffer);

	CleanupStoreFile:
	fclose(StoreFile);
	return Result;
}