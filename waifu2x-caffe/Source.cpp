#include <stdio.h>
#include <tclap/CmdLine.h>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <functional>
#include <boost/tokenizer.hpp>
#include "../common/waifu2x.h"


// http://stackoverflow.com/questions/10167382/boostfilesystem-get-relative-path
boost::filesystem::path relativePath(const boost::filesystem::path &path, const boost::filesystem::path &relative_to)
{
	// create absolute paths
	boost::filesystem::path p = boost::filesystem::absolute(path);
	boost::filesystem::path r = boost::filesystem::absolute(relative_to);

	// if root paths are different, return absolute path
	if (p.root_path() != r.root_path())
		return p;

	// initialize relative path
	boost::filesystem::path result;

	// find out where the two paths diverge
	boost::filesystem::path::const_iterator itr_path = p.begin();
	boost::filesystem::path::const_iterator itr_relative_to = r.begin();
	while (*itr_path == *itr_relative_to && itr_path != p.end() && itr_relative_to != r.end()) {
		++itr_path;
		++itr_relative_to;
	}

	// add "../" for each remaining token in relative_to
	if (itr_relative_to != r.end()) {
		++itr_relative_to;
		while (itr_relative_to != r.end()) {
			result /= "..";
			++itr_relative_to;
		}
	}

	// add remaining path
	while (itr_path != p.end()) {
		result /= *itr_path;
		++itr_path;
	}

	return result;
}

int main(int argc, char** argv)
{
	// definition of command line arguments
	TCLAP::CmdLine cmd("waifu2x reimplementation using Caffe", ' ', "1.0.0");

	TCLAP::ValueArg<std::string> cmdInputFile("i", "input_path",
		"path to input image file", true, "",
		"string", cmd);

	TCLAP::ValueArg<std::string> cmdOutputFile("o", "output_path",
		"path to output image file (when input_path is folder, output_path must be folder)", false,
		"(auto)", "string", cmd);

	TCLAP::ValueArg<std::string> cmdInputFileExt("l", "input_extention_list",
		"extention to input image file when input_path is folder", false, "png:jpg:jpeg:tif:tiff:bmp",
		"string", cmd);

	TCLAP::ValueArg<std::string> cmdOutputFileExt("e", "output_extention",
		"extention to output image file when output_path is (auto) or input_path is folder", false,
		"png", "string", cmd);

	std::vector<std::string> cmdModeConstraintV;
	cmdModeConstraintV.push_back("noise");
	cmdModeConstraintV.push_back("scale");
	cmdModeConstraintV.push_back("noise_scale");
	cmdModeConstraintV.push_back("auto_scale");
	TCLAP::ValuesConstraint<std::string> cmdModeConstraint(cmdModeConstraintV);
	TCLAP::ValueArg<std::string> cmdMode("m", "mode", "image processing mode",
		false, "noise_scale", &cmdModeConstraint, cmd);

	std::vector<int> cmdNRLConstraintV;
	cmdNRLConstraintV.push_back(1);
	cmdNRLConstraintV.push_back(2);
	TCLAP::ValuesConstraint<int> cmdNRLConstraint(cmdNRLConstraintV);
	TCLAP::ValueArg<int> cmdNRLevel("n", "noise_level", "noise reduction level",
		false, 1, &cmdNRLConstraint, cmd);

	TCLAP::ValueArg<double> cmdScaleRatio("s", "scale_ratio",
		"custom scale ratio", false, 2.0, "double", cmd);

	TCLAP::ValueArg<std::string> cmdModelPath("", "model_dir",
		"path to custom model directory (don't append last / )", false,
		"models", "string", cmd);

	std::vector<std::string> cmdProcessConstraintV;
	cmdProcessConstraintV.push_back("cpu");
	cmdProcessConstraintV.push_back("gpu");
	cmdProcessConstraintV.push_back("cudnn");
	TCLAP::ValuesConstraint<std::string> cmdProcessConstraint(cmdProcessConstraintV);
	TCLAP::ValueArg<std::string> cmdProcess("p", "process", "process mode",
		false, "gpu", &cmdProcessConstraint, cmd);

	// definition of command line argument : end

	TCLAP::Arg::enableIgnoreMismatched();

	// parse command line arguments
	try
	{
		cmd.parse(argc, argv);
	}
	catch (std::exception &e)
	{
		printf("�G���[: %s\n", e.what());
		return 1;
	}

	const boost::filesystem::path input_path(boost::filesystem::absolute((cmdInputFile.getValue())));

	std::string outputExt = cmdOutputFileExt.getValue();
	if (outputExt.length() > 0 && outputExt[0] != '.')
		outputExt = "." + outputExt;

	std::vector<InputOutputPathPair> file_paths;
	if (boost::filesystem::is_directory(input_path)) // input_path���t�H���_�Ȃ炻�̃f�B���N�g���ȉ��̉摜�t�@�C�����ꊇ�ϊ�
	{
		boost::filesystem::path output_path;

		if (cmdOutputFile.getValue() == "(auto)")
		{
			// �utest�v�Ȃ�utest_noise_scale(Level1)(x2.000000)�v�݂����Ȋ����ɂ���

			std::string addstr("_" + cmdMode.getValue());

			const std::string &mode = cmdMode.getValue();
			if (mode.find("noise") != mode.npos || mode.find("auto_scale") != mode.npos)
				addstr += "(Level" + std::to_string(cmdNRLevel.getValue()) + ")";
			if (mode.find("scale") != mode.npos)
				addstr += "(x" + std::to_string(cmdScaleRatio.getValue()) + ")";

			output_path = input_path.branch_path() / (input_path.stem().string() + addstr);
		}
		else
			output_path = cmdOutputFile.getValue();

		output_path = boost::filesystem::absolute(output_path);

		if (!boost::filesystem::exists(output_path))
		{
			if (!boost::filesystem::create_directory(output_path))
			{
				printf("�G���[: �o�̓t�H���_�u%s�v�̍쐬�Ɏ��s���܂���\n", output_path.string().c_str());
				return 1;
			}
		}

		std::vector<std::string> extList;
		{
			// input_extention_list�𕶎���̔z��ɂ���

			typedef boost::char_separator<char> char_separator;
			typedef boost::tokenizer<char_separator> tokenizer;

			char_separator sep(":", "", boost::drop_empty_tokens);
			tokenizer tokens(cmdInputFileExt.getValue(), sep);

			for (tokenizer::iterator tok_iter = tokens.begin(); tok_iter != tokens.end(); ++tok_iter)
				extList.push_back("." + *tok_iter);
		}

		// �ϊ�����摜�̓��́A�o�̓p�X���擾
		const auto func = [&extList, &input_path, &output_path, &outputExt, &file_paths](const boost::filesystem::path &path)
		{
			BOOST_FOREACH(const boost::filesystem::path& p, std::make_pair(boost::filesystem::recursive_directory_iterator(path),
				boost::filesystem::recursive_directory_iterator()))
			{
				if (boost::filesystem::is_directory(p))
				{
					const auto out_relative = relativePath(p, input_path);
					const auto out_absolute = output_path / out_relative;

					if (!boost::filesystem::exists(out_absolute))
					{
						if (!boost::filesystem::create_directory(out_absolute))
						{
							printf("�G���[: �o�̓t�H���_�u%s�v�̍쐬�Ɏ��s���܂���\n", out_absolute.string().c_str());
							return false;
						}
					}
				}
				else if (std::find(extList.begin(), extList.end(), p.extension().string()) != extList.end())
				{
					const auto out_relative = relativePath(p, input_path);
					const auto out_absolute = output_path / out_relative;

					const auto out = (out_absolute.branch_path() / out_absolute.stem()).string() + outputExt;

					file_paths.emplace_back(p.string(), out);
				}
			}

			return true;
		};

		if (!func(input_path))
			return 1;
	}
	else
	{
		std::string outputFileName = cmdOutputFile.getValue();

		if (outputFileName == "(auto)")
		{
			// �umiku_small.png�v�Ȃ�umiku_small(noise_scale)(Level1)(x2.000000).png�v�݂����Ȋ����ɂ���

			outputFileName = cmdInputFile.getValue();
			const auto tailDot = outputFileName.find_last_of('.');
			outputFileName.erase(tailDot, outputFileName.length());
			outputFileName = outputFileName + "(" + cmdMode.getValue() + ")";
			std::string &mode = cmdMode.getValue();
			if (mode.find("noise") != mode.npos || mode.find("auto_scale") != mode.npos)
				outputFileName = outputFileName + "(Level" + std::to_string(cmdNRLevel.getValue()) + ")";
			if (mode.find("scale") != mode.npos)
				outputFileName = outputFileName + "(x" + std::to_string(cmdScaleRatio.getValue()) + ")";
			outputFileName += outputExt;
		}

		file_paths.emplace_back(cmdInputFile.getValue(), outputFileName);
	}

	std::vector<PathAndErrorPair> errors;

	const eWaifu2xError ret = waifu2x(argc, argv, file_paths, cmdMode.getValue(), cmdNRLevel.getValue(), cmdScaleRatio.getValue(), cmdModelPath.getValue(), cmdProcess.getValue(), errors);
	if (ret != eWaifu2xError_OK || errors.size() > 0)
	{
		switch (ret)
		{
		case eWaifu2xError_InvalidParameter:
			printf("�G���[: �p�����[�^���s���ł�\n");
			break;
		case eWaifu2xError_FailedOpenModelFile:
			printf("�G���[: ���f���t�@�C�����J���܂���ł���\n");
			break;
		case eWaifu2xError_FailedParseModelFile:
			printf("�G���[: ���f���t�@�C�������Ă��܂�\n");
			break;
		case eWaifu2xError_FailedConstructModel:
			printf("�G���[: �l�b�g���[�N�̍\�z�Ɏ��s���܂���\n");
			break;
		}

		for (const auto &ep : errors)
		{
			const auto &fp = ep.first;

			switch (ep.second)
			{
			case eWaifu2xError_InvalidParameter:
				printf("�G���[: �p�����[�^���s���ł�\n");
				break;
			case eWaifu2xError_FailedOpenInputFile:
				printf("�G���[: ���͉摜�u%s�v���J���܂���ł���\n", fp.first.c_str());
				break;
			case eWaifu2xError_FailedOpenOutputFile:
				printf("�G���[: �o�͉摜�u%s�v���������߂܂���ł���\n", fp.second.c_str());
				break;
			case eWaifu2xError_FailedProcessCaffe:
				printf("�G���[: ��ԏ����Ɏ��s���܂���\n");
				break;
			}
		}

		printf("�ϊ��Ɏ��s���܂���\n");
		return 1;
	}

	return 0;
}